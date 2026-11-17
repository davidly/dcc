#!/usr/bin/env python3
"""Validate checked-in test baselines with a host C compiler (Python 3.7+).

Baselines are never rewritten. Uses MSVC on Windows, clang on macOS, gcc on
Linux, with CC/--cc overrides on Unix. Probes GCC -m32 on Linux. Honors host
exclusions, per-app host-cflags, arguments, stdin and baseline placeholders.
Compiles and runs apps concurrently, staging only each app's declared fixtures.
Relative paths are rooted at the checkout, regardless of the caller's cwd.
"""
import argparse
import concurrent.futures
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

try:
    from . import runall
except ImportError:
    import runall

ROOT = Path(__file__).resolve().parents[1]
PLACEHOLDERS = {'DATE': r'[A-Z][a-z]{2}\s+\d{1,2}\s+\d{4}',
                'TIME': r'\d{2}:\d{2}:\d{2}', 'SEP': r'[/\\]', 'UINT': r'\d+'}
CPM_ONLY = [
    (r'(?m)^\s*#\s*(?:asm|endasm)\b', '#asm/#endasm Z80 inline assembly'),
    (r'(?<![A-Za-z0-9_])(?:bdos\s*\(|__BDOS\b)', 'BDOS call'),
    (r'(?<![A-Za-z0-9_])bios\s*\(', 'BIOS call'),
    (r'(?<![A-Za-z0-9_])inp\s*\(', 'Z80 port input'),
    (r'(?<![A-Za-z0-9_])outp\s*\(', 'Z80 port output'),
    (r'(?<![A-Za-z0-9_])getch\s*\(', 'CP/M non-echo console input'),
    (r'(?<![A-Za-z0-9_])kbhit\s*\(', 'CP/M console polling'),
    (r'(?<![A-Za-z0-9_])tmpnam\s*\(', 'CP/M 8.3 limit'),
    (r'(?<![A-Za-z0-9_])execv\s*\(', 'CP/M execv'),
    (r'(?<![A-Za-z0-9_])strftime\s*\(', 'CP/M strftime'),
    (r'(?<![A-Za-z0-9_])assert\s*\(', 'CP/M assert'),
    (r'(?<![A-Za-z0-9_])abort\s*\(', 'CP/M abort'),
    (r'\*\s*\([^)]*\*\)\s*6\b', 'CP/M BDOS vector memory access'),
]


def positive(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError('must be positive')
    return number


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', '-BuildDir', default='build/host-validate')
    p.add_argument('--baseline-dir', '-BaselineDir', default='tests/baselines')
    p.add_argument('--cc', '-CC')
    p.add_argument('--app', '-App')
    p.add_argument('--timeout', '--run-timeout', '-RunTimeout', type=positive, default=10)
    p.add_argument('--serial', '-Serial', action='store_true')
    p.add_argument('--jobs', '--throttle-limit', '-ThrottleLimit', type=positive, default=os.cpu_count() or 1,
                   help='concurrent apps (default: logical CPU count)')
    p.add_argument('-Help', action='help', help=argparse.SUPPRESS)
    return p.parse_args(argv)


def matches_baseline(actual, baseline):
    pieces, end = [], 0
    for match in re.finditer(r'\{\{([A-Z]+)\}\}', baseline):
        pieces.append(re.escape(baseline[end:match.start()]))
        pieces.append(PLACEHOLDERS.get(match.group(1), re.escape(match.group())))
        end = match.end()
    pieces.append(re.escape(baseline[end:]))
    return re.fullmatch(''.join(pieces), actual) is not None


def cpm_reason(source):
    return next((reason for pattern, reason in CPM_ONLY if re.search(pattern, source)), None)


def skip_config(override, kind, system, m32):
    if override.get('ignore'):
        return 'config'
    if override.get('host') and not (m32 and override.get('requires-32bit-linux-host-compiler')):
        return 'host'
    if kind == 'msvc' and override.get('requires-non-msvc-host-compiler'):
        return 'host'
    if system == 'Darwin' and override.get('requires-non-macos-host-compiler'):
        return 'host'
    return None


def host_compiler(args, env, system, build):
    env = dict(env)
    if system == 'Windows':
        spec = importlib.util.spec_from_file_location('build_dcc', ROOT/'scripts/build-dcc.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        try:
            vcvars = builder.find_msvc(env, builder.native_arch(env))
            env = builder.msvc_environment(vcvars, env)
        except builder.BuildError as error:
            raise RuntimeError(str(error))
        command = shutil.which('cl', path=builder.env_path(env))
        if not command:
            raise RuntimeError('MSVC cl.exe not found after vcvars setup')
        return {'kind':'msvc', 'command':command, 'flags':[], 'env':env, 'version':'MSVC ({})'.format(vcvars)}
    command = args.cc or env.get('CC') or ('clang' if system == 'Darwin' else 'gcc')
    resolved = shutil.which(command)
    if not resolved:
        raise RuntimeError('C compiler not found: '+command)
    version = subprocess.run([resolved,'--version'], env=env, capture_output=True, text=True, check=True).stdout.splitlines()[0]
    flags = []
    if system == 'Linux' and re.search('gcc|GNU', version, re.I):
        with tempfile.TemporaryDirectory(prefix='.m32-probe-', dir=str(build)) as folder:
            folder = Path(folder)
            source = folder/'probe.c'
            source.write_text('int main(void) { return sizeof(void *) == 4 ? 0 : 1; }')
            probe = subprocess.run([resolved,'-m32',str(source),'-o',str(folder/'probe')],
                                   env=env, capture_output=True)
            if probe.returncode == 0 and (folder/'probe').is_file():
                flags = ['-m32']
    return {'kind':'unix', 'command':resolved, 'flags':flags, 'env':env, 'version':version+(' (-m32)' if flags else '')}


def compile_command(compiler, source, exe, override, root, system):
    prelude = str(root/'tests/host-validate-prelude.h')
    if compiler['kind'] == 'msvc':
        return [compiler['command'], '/nologo', '/w', '/O2', '/Zc:__STDC__', '/std:c11',
                '/I',str(root/'tests/msvc-shims'),'/FI',prelude,
                '/Fe:'+str(exe),'/Fo:'+str(exe.with_suffix('.obj')),str(source)]
    flags = shlex.split(override.get('host-cflags') or compiler['env'].get('CFLAGS') or '-std=gnu99 -w -O2')
    if '-fsigned-char' not in flags:
        flags.append('-fsigned-char')
    if system == 'Darwin' and '-fno-common' not in flags:
        flags.append('-fno-common')
    return [compiler['command']] + flags + compiler['flags'] + ['-include',prelude,str(source),'-o',str(exe),'-lm']


def fixture_files(root, specs):
    """Index declared inputs only, including C sources used by interpreters."""
    wanted = {str(item if isinstance(item, str) else item['name']).lower()
              for item in specs if isinstance(item, str) or not item.get('source')}
    sources = {}
    for folder in (root/'tests', root):
        if not wanted:
            break
        for path in folder.iterdir():
            key = path.name.lower()
            if key in wanted and path.is_file():
                sources[key] = path
                wanted.remove(key)
    return sources


def validate_app(name, compiler, fixtures, build, baselines, timeout, override, root, system):
    start = time.monotonic()
    def result(status, lines):
        return name,status,time.monotonic()-start,lines
    try:
        source = Path('tests')/(name+'.c')  # Preserve __FILE__ in existing baselines.
        reason = cpm_reason((root/source).read_text())
        if reason:
            return result('Skipped', ['SKIP: '+reason])
        work = build/name
        work.mkdir(parents=True, exist_ok=True)
        for item in override.get('fixtures', []):
            fixture_name = item if isinstance(item, str) else item['name']
            explicit = item.get('source') if isinstance(item, dict) else None
            fixture = root/explicit if explicit else fixtures.get(fixture_name.lower())
            if fixture is None or not fixture.is_file():
                return result('Failed', ['ERROR: fixture not found: '+fixture_name])
            for filename in dict.fromkeys((fixture_name,fixture_name.upper(),fixture_name.lower())):
                shutil.copyfile(fixture, work/filename)
        exe = work/(name+('.exe' if system == 'Windows' else ''))
        if exe.exists():
            exe.unlink()
        command = compile_command(compiler,source,exe,override,root,system)
        code,timed,output = runall.run(command,root,60,env=compiler['env'])
        if code or timed or not exe.is_file():
            return result('Failed', ['COMPILE FAILED']+output.splitlines()[:5])
        code,timed,output = runall.run([str(exe)]+str(override.get('args') or '').split(),work,timeout,
                                      override.get('stdin') or '',env=compiler['env'])
        if timed:
            return result('Failed', ['ERROR: host app timed out after {} seconds'.format(timeout)])
        baseline = baselines/(name+'.txt')
        if not baseline.is_file():
            return result('Failed',['ERROR: no baseline at '+str(baseline)])
        lines = ['WARNING: host app exited with code {}'.format(code)] if code else []
        if not matches_baseline(runall.normal(output),runall.normal(baseline.read_text())):
            return result('Failed',lines+['OUTPUT MISMATCH (vs {})'.format(baseline),'Got: '+' | '.join(output.splitlines()[:3])])
        return result('Passed',lines+['Output matches baseline'])
    except OSError as error:
        return result('Failed',[str(error)])


def main(argv=None):
    args = parse_args(argv)
    build = (ROOT/args.build_dir).resolve()
    baselines = ROOT/args.baseline_dir
    try:
        build.mkdir(parents=True,exist_ok=True)
        config = ROOT/'tests/_test_overrides.json'
        overrides = {a['name']:a for a in json.loads(config.read_text())['apps']} if config.is_file() else {}
        apps = [args.app] if args.app else sorted(p.stem for p in (ROOT/'tests').glob('*.c'))
        if not apps or any(not (ROOT/'tests'/(name+'.c')).is_file() for name in apps):
            raise RuntimeError('No source files found for requested host validation')
        system = platform.system()
        compiler = host_compiler(args,os.environ,system,build)
        print('Host compiler: '+compiler['version'],flush=True)
        print('Build artifacts: '+str(build),flush=True)
        counts = {'Passed':0,'Failed':0,'Skipped':0,'config':0,'host':0}
        selected = []
        for name in apps:
            skip = skip_config(overrides.get(name,{}),compiler['kind'],system,'-m32' in compiler['flags'])
            if skip:
                counts[skip] += 1
            else:
                selected.append(name)
        fixtures = fixture_files(ROOT, [item for name in selected
                                       for item in overrides.get(name, {}).get('fixtures', [])])
        workers = min(len(selected) or 1, 1 if args.serial else args.jobs)
        print('Parallel workers: {}'.format(workers), flush=True)
        started = time.monotonic()
        def one(name):
            return validate_app(name,compiler,fixtures,build,baselines,args.timeout,
                                overrides.get(name,{}),ROOT,system)
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
            pending = [pool.submit(one,name) for name in selected]
            for future in concurrent.futures.as_completed(pending):
                name,status,elapsed,lines = future.result()
                counts[status] += 1
                print('  {}  {:<16} {:.2f}s'.format({'Passed':'PASS','Failed':'FAIL','Skipped':'SKIP'}[status],name,elapsed),flush=True)
                for line in lines:
                    print('    '+line,flush=True)
        print('\nHOST VALIDATION SUMMARY\n  Total apps:       {}'.format(len(apps)))
        for label,key in [('Passed','Passed'),('Failed','Failed'),('Skipped config','config'),('Skipped host','host'),('Skipped CP/M','Skipped')]:
            print('  {:<18}{}'.format(label+':',counts[key]))
        print('  Total time:       {:.2f}s'.format(time.monotonic()-started))
        if counts['Failed']:
            return 1
        print('>>> SUCCESS: All runnable host validations passed <<<')
        return 0
    except (OSError,RuntimeError,subprocess.SubprocessError,ValueError) as error:
        print('ERROR: '+str(error),file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
