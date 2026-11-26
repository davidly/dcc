#!/usr/bin/env python3
"""Build one CP/M application using Python 3.7+ and the native dccmake tool.

Usage: ma APP [fast|nopeep|full] [--source-path FILE] [--build-dir DIR]
The default is fast (peephole optimized). Full builds fast then nopeep,
leaving the unoptimized COM last, as the original ma wrappers did.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FAST = ('fast','peep','opt','optimized','o','-o','1','yes','true')
NOPEEP = ('nopeep','nop','noopt','unopt','u','-u','0','no','false')


class BuildError(Exception):
    pass


# String and character literals, then block and line comments. Literals are
# matched first so a "/*" or "//" inside one isn't taken for a comment.
C_LITERAL_OR_COMMENT = re.compile(
    r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|/\*.*?\*/|//[^\n]*', re.S)


def without_c_comments(text):
    """Blank out C comments so the printf-format scan only sees code and
    literals: a comment like "about 1% faster" isn't a %f format."""
    return C_LITERAL_OR_COMMENT.sub(
        lambda m: m.group(0) if m.group(0)[0] in '"\'' else ' ', text)


def mode(value):
    value = value.lower()
    if value in FAST:
        return 'fast'
    if value in NOPEEP:
        return 'nopeep'
    if value == 'full':
        return value
    raise argparse.ArgumentTypeError('expected fast/peep, nopeep, or full')


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, epilog=(
        'Environment: DCCMAKE, DCC, DCCPEEP, DCCRTLSTRIP, M80C, L80C, NTVCM, M80, L80; '
        'DCC_HOME, DCC_LIB, DCC_INCLUDE, DCC_RUNTIME; DCC_STACK_SIZE, DCC_FORCE_STACK_CHECK, '
        'DCC_FLOATIO, DCC_LONGIO, DCC_ARGS, DCC_STRIP_UNUSED, NTVCM_ARGS. '
        'Build the host tools with m.sh/m.bat before using this helper.'))
    parser.add_argument('name', nargs='?', help='test name or C source file')
    parser.add_argument('positional_mode', nargs='?', type=mode)
    parser.add_argument('-Name', dest='named_name', help=argparse.SUPPRESS)
    parser.add_argument('--mode', '-Mode', type=mode)
    parser.add_argument('-u', dest='mode', action='store_const', const='nopeep', help=argparse.SUPPRESS)
    parser.add_argument('-O', '-o', dest='mode', action='store_const', const='fast', help=argparse.SUPPRESS)
    parser.add_argument('--source-path', '-SourcePath')
    parser.add_argument('--build-dir', '-BuildDir', default='build')
    parser.add_argument('--emulator', '-Emulator', help='NTVCM override for emulated M80/L80')
    parser.add_argument('--emulated-m80', '--use-emulated-m80', '-EmulatedM80', '-UseEmulatedM80', action='store_true')
    parser.add_argument('--emulated-l80', '--use-emulated-l80', '-EmulatedL80', '-UseEmulatedL80', action='store_true')
    parser.add_argument('--stack-size', '-StackSize', type=int)
    args = parser.parse_intermixed_args(argv)
    if args.named_name:
        if args.name:
            parser.error('provide the app name once')
        args.name = args.named_name
    if not args.name:
        parser.error('an app name is required')
    if args.stack_size is not None and args.stack_size < 0:
        parser.error('--stack-size must not be negative')
    args.mode = args.mode or args.positional_mode or 'fast'
    return args


def find_source(args):
    if args.source_path:
        path = Path(args.source_path)
        if not path.is_file():
            raise BuildError('Source file not found: ' + str(path))
        return path
    path = Path(args.name)
    base = path.with_suffix('') if path.suffix else path
    candidates = []
    if path.parent != Path('.'):
        candidates.extend([base.with_suffix('.c'), base.with_suffix('.C')])
    for folder in (Path('tests'), Path('.')):
        for name in (base.name+'.c', base.name+'.C', base.name.lower()+'.c', base.name.upper()+'.C'):
            candidates.append(folder/name)
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise BuildError('Source file not found for: ' + args.name)


def unique_paths(paths):
    result = []
    for path in paths:
        if not str(path).strip():
            continue
        path = Path(path).resolve()
        if path not in result:
            result.append(path)
    return result


def path_list(value):
    return [Path(x.strip()) for x in value.split(os.pathsep) if x.strip()]


def resolve_command(value):
    # Resolve path-like overrides before native tools change into build/.
    if '/' in value or '\\' in value or Path(value).is_absolute():
        return str(Path(value).resolve())
    found = shutil.which(value)
    return str(Path(found).resolve()) if found else value


def tool(name, env, package_home):
    variable = 'DCCMAKE' if name == 'dccmake' else name.upper()
    if env.get(variable):
        return resolve_command(env[variable])
    suffix = '.exe' if os.name == 'nt' else ''
    folders = [Path.cwd()]
    if package_home:
        folders += [package_home/'bin', package_home]
    folders += [ROOT, ROOT/'bin']
    for folder in folders:
        path = folder/(name+suffix)
        if path.is_file():
            return str(path.resolve())
    return resolve_command(name)


def build_commands(args, env):
    source = find_source(args)
    package_home = Path(env['DCC_HOME']).resolve() if env.get('DCC_HOME') else None
    assets = [Path.cwd()]
    if package_home:
        assets += [package_home, package_home/'lib']
    assets += path_list(env.get('DCC_LIB','')) + [ROOT, ROOT/'lib']
    assets = unique_paths(assets)
    runtime = Path(env['DCC_RUNTIME']) if env.get('DCC_RUNTIME') else next(
        (folder/'DCCRTL.MAC' for folder in assets if (folder/'DCCRTL.MAC').is_file()), None)
    if runtime is None or not runtime.is_file():
        raise BuildError('Runtime not found: ' + (env.get('DCC_RUNTIME') or 'DCCRTL.MAC'))
    includes = [package_home/'include'] if package_home else []
    includes += path_list(env.get('DCC_INCLUDE',''))
    for folder in assets:
        if (folder/'include').is_dir():
            includes.append(folder/'include')
        elif (folder/'stdio.h').is_file():
            includes.append(folder)
    includes = unique_paths(includes)
    contents = without_c_comments(source.read_text(errors='replace'))
    floatio = env.get('DCC_FLOATIO') == '1' or bool(re.search(r'%[-+ #0-9.*]*[fF]', contents))
    longio = env.get('DCC_LONGIO') == '1' or bool(re.search(r'%[-+ #0-9.*]*l[duxXs]', contents, re.I))
    stack = args.stack_size if args.stack_size is not None else env.get('DCC_STACK_SIZE')
    commands = []
    for selected in (('fast','nopeep') if args.mode=='full' else (args.mode,)):
        command = [tool('dccmake',env,package_home), '--no-config',
                   'dcc-input='+str(source), 'dcc-output='+Path(args.name).stem,
                   'dcc-build-dir='+args.build_dir, 'dcc-peep='+str(selected=='fast').lower(),
                   'dcc-runtime='+str(runtime), 'dcc-floatio='+str(floatio).lower(),
                   'dcc-flongio='+str(longio).lower(),
                   'dcc-strip-unused='+str(env.get('DCC_STRIP_UNUSED')!='0').lower()]
        for name in ('dcc','dccpeep','dccrtlstrip','m80c','l80c'):
            command.append(name+'-tool='+tool(name,env,package_home))
        command.append('ntvcm-tool='+resolve_command(args.emulator or env.get('NTVCM') or 'ntvcm'))
        for name in ('m80','l80'):
            value = env.get(name.upper()) or name
            if '/' in value or '\\' in value or Path(value).is_absolute():
                value = str(Path(value).resolve())
            else:
                filename = value if value.lower().endswith('.com') else value+'.com'
                value = next((str(folder/filename) for folder in assets if (folder/filename).is_file()), value)
            command.append(name+'-command='+value)
            emulated = getattr(args,'emulated_'+name) or env.get('DCC_USE_EMULATED_'+name.upper())=='1'
            command.append('dcc-use-emulated-'+name+'='+str(emulated).lower())
        if stack is not None and str(stack):
            command += ['-s',str(stack)]
        for directory in includes:
            command += ['-I',str(directory)]
        commands.append(command)
    return commands


def main(argv=None):
    args = parse_args(argv)
    try:
        commands = build_commands(args, os.environ)
        Path(args.build_dir).mkdir(parents=True, exist_ok=True)
        for command in commands:
            result = subprocess.run(command)
            if result.returncode:
                return result.returncode
        return 0
    except (BuildError,OSError) as error:
        print('ERROR: '+str(error),file=sys.stderr)
        return 1


if __name__=='__main__':
    sys.exit(main())
