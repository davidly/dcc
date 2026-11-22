#!/usr/bin/env python3
"""Build the native DCC toolchain on Python 3.7+ (Windows, Linux, macOS).

Compile all six C tools in a bounded parallel wave, then link them and build
the CMake debugger host and example adapter. Outputs go to the repository
root; objects, listings and debug information stay under --output-path.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import locale
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TARGETS = ('dcc', 'dccpeep', 'dccrtlstrip', 'dccmake', 'm80c', 'l80c')


class BuildError(Exception):
    pass


def windows_env(env, name, default=''):
    return next((value for key, value in env.items() if key.upper() == name.upper()), default)


def windows_native_machine():
    """Get the OS architecture even when Python itself runs under emulation."""
    if os.name != 'nt':
        return None
    try:
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.GetCurrentProcess.restype = wintypes.HANDLE
        probe = kernel.IsWow64Process2
        probe.argtypes = (wintypes.HANDLE, ctypes.POINTER(ctypes.c_ushort),
                          ctypes.POINTER(ctypes.c_ushort))
        probe.restype = wintypes.BOOL
        process, native = ctypes.c_ushort(), ctypes.c_ushort()
        if probe(kernel.GetCurrentProcess(), ctypes.byref(process), ctypes.byref(native)):
            return {0xAA64: 'arm64', 0x8664: 'amd64', 0x014C: 'x86'}.get(native.value)
    except (AttributeError, OSError, ValueError):
        pass  # Older Windows: fall back to the OS-provided environment.
    return None


def native_arch(env):
    return (windows_native_machine() or windows_env(env, 'PROCESSOR_ARCHITEW6432') or
            windows_env(env, 'PROCESSOR_ARCHITECTURE') or platform.machine()).lower()


def msvc_toolchain(arch):
    arm = arch.lower() in ('arm64', 'aarch64')
    return {
        'component': 'Microsoft.VisualStudio.Component.VC.Tools.' + ('ARM64' if arm else 'x86.x64'),
        'vcvars': 'vcvarsarm64.bat' if arm else 'vcvars64.bat',
        'description': 'MSVC ARM64 C++ toolchain' if arm else 'MSVC x64 C++ toolchain',
    }


def find_msvc(env, arch):
    toolchain = msvc_toolchain(arch)
    roots = [Path(windows_env(env, key)) for key in ('ProgramFiles', 'ProgramFiles(x86)')
             if windows_env(env, key)]
    for root in reversed(roots):
        vswhere = root / 'Microsoft Visual Studio' / 'Installer' / 'vswhere.exe'
        if not vswhere.is_file():
            continue
        result = subprocess.run([str(vswhere), '-latest', '-prerelease', '-products', '*',
                                 '-requires', toolchain['component'], '-property', 'installationPath',
                                 '-utf8'], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode == 0:
            for line in result.stdout.decode('utf-8-sig', errors='replace').splitlines():
                candidate = Path(line.strip()) / 'VC' / 'Auxiliary' / 'Build' / toolchain['vcvars']
                if candidate.is_file():
                    return candidate
    for root in roots:
        for version in ('18', '2022', '2019', '17'):
            for edition in ('Community', 'Professional', 'Enterprise', 'BuildTools', 'Preview'):
                candidate = (root / 'Microsoft Visual Studio' / version / edition /
                             'VC' / 'Auxiliary' / 'Build' / toolchain['vcvars'])
                if candidate.is_file():
                    return candidate
    raise BuildError('Could not find {}. Install Visual Studio Build Tools with '
                     'Desktop development with C++ and component {} (including recommended '
                     'components), then rerun this build.'.format(toolchain['description'], toolchain['component']))


def visual_studio_cmake(vcvars):
    candidate = (vcvars.parents[3] / 'Common7' / 'IDE' / 'CommonExtensions' /
                 'Microsoft' / 'CMake' / 'CMake' / 'bin' / 'cmake.exe')
    return candidate if candidate.is_file() else None


def msvc_environment(vcvars, env):
    # cmd /u makes SET's environment dump UTF-16, preserving non-ASCII paths.
    # The temporary batch is ASCII: the actual VS path travels via the Unicode
    # process environment, never through an ANSI-encoded batch-file literal.
    with tempfile.TemporaryDirectory(prefix='dcc-msvc-') as directory:
        batch = Path(directory) / 'environment.cmd'
        batch.write_text('@echo off\ncall "%DCC_VCVARS_BAT%" >nul\n'
                         'if errorlevel 1 exit /b %errorlevel%\nset\n', encoding='ascii')
        setup_env = dict(env, DCC_VCVARS_BAT=str(vcvars))
        command = '"{}" /d /u /s /c ""{}""'.format(windows_env(env, 'COMSPEC', 'cmd.exe'), batch)
        result = subprocess.run(command, env=setup_env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise BuildError('MSVC environment setup failed ({}): {}'.format(
            result.returncode, result.stderr.decode(locale.getpreferredencoding(False), errors='replace')))
    configured = dict(env)
    for line in result.stdout.decode('utf-16-le').splitlines():
        key, separator, value = line.partition('=')
        if key and separator and key.upper() != 'DCC_VCVARS_BAT':
            # Environment names are case-insensitive on Windows, even when
            # this is an ordinary Python dict rather than os.environ.
            for old in list(configured):
                if old.upper() == key.upper():
                    del configured[old]
            configured[key] = value
    return configured


def env_path(env):
    return windows_env(env, 'PATH')


def invoke(argv, cwd, env, verbose=False, description=None):
    if verbose:
        print('+ ' + subprocess.list2cmdline([str(x) for x in argv]), flush=True)
    elif description:
        print('  ' + description, flush=True)
    try:
        result = subprocess.run([str(x) for x in argv], cwd=str(cwd), env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                universal_newlines=True, errors='replace')
    except OSError as error:
        raise BuildError('{}: {}'.format(argv[0], error))
    if result.stdout and (verbose or result.returncode):
        print(result.stdout.rstrip(), flush=True)
    if result.returncode:
        raise BuildError('{} failed with exit code {}'.format(description or argv[0], result.returncode))
    return result.stdout


def parallel_wave(jobs, workers, root, env, verbose):
    """Capture each process independently and replay output in source order."""
    def one(item):
        name, argv = item
        try:
            result = subprocess.run([str(x) for x in argv], cwd=str(root), env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    universal_newlines=True, errors='replace')
            return name, argv, result.returncode, result.stdout
        except OSError as error:
            return name, argv, 1, str(error)
    failed = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        for name, argv, code, output in pool.map(one, jobs):
            print('  ' + name, flush=True)
            if verbose:
                print('+ ' + subprocess.list2cmdline([str(x) for x in argv]), flush=True)
            if output:
                print(output.rstrip(), flush=True)
            if code:
                failed.append('{} (exit {})'.format(name, code))
    if failed:
        raise BuildError('Build failed: ' + ', '.join(failed))


def unix_compiler(args, env, system):
    requested = args.cc or env.get('CC') or ('clang' if system == 'Darwin' else 'gcc')
    # Accept a quoted compiler command (e.g. CC='ccache gcc') as well as an
    # executable path containing spaces, without invoking a shell.
    command = [requested] if shutil.which(requested, path=env_path(env)) else shlex.split(requested)
    if not command or not shutil.which(command[0], path=env_path(env)):
        hint = ('xcode-select --install' if system == 'Darwin' else
                'install build-essential (Debian/Ubuntu), or your distribution\'s C toolchain')
        raise BuildError('C compiler {!r} was not found; {}. Use --cc to select another compiler.'.format(requested, hint))
    version = invoke(command + ['--version'], ROOT, env)
    print('C compiler: ' + version.splitlines()[0], flush=True)
    flags = shlex.split(env['CFLAGS']) if env.get('CFLAGS') else ['-std=c11', '-w', '-O2', '-g']
    if system == 'Darwin' and '-fno-common' not in flags:
        flags.append('-fno-common')
    if 'gcc' in version.lower() and not env.get('CFLAGS'):
        flags += ['-Wno-misleading-indentation', '-Wno-format-overflow', '-Wno-stringop-truncation']
    return command, flags


def build_tools(root, output, args, env, system):
    version = invoke([sys.executable, Path(__file__).with_name('generate-version.py'),
                      '--root', root, '--output', output / 'dcc_version.h'], root, env).strip()
    print('DCC version: ' + version, flush=True)
    windows = system == 'Windows'
    vcvars = None
    if windows:
        arch = native_arch(env)
        vcvars = find_msvc(env, arch)
        print('Found {}: {}'.format(msvc_toolchain(arch)['description'], vcvars), flush=True)
        env = msvc_environment(vcvars, env)
        compiler = shutil.which('cl.exe', path=env_path(env))
        if not compiler:
            raise BuildError('MSVC cl.exe not found after vcvars setup.')
        compiler = [compiler]
        flags = ['/nologo', '/GS-', '/GL', '/Oti2', '/Ob3', '/Qpar', '/FAsc', '/Zi', '/FS', '/std:c11']
    else:
        compiler, flags = unix_compiler(args, env, system)
    compiles, links, outputs = [], [], []
    for name in TARGETS:
        source = root / 'src' / name
        sources = sorted(source.glob('*.c'))
        if not sources:
            raise BuildError('No C sources found in ' + str(source))
        objdir = output / name
        objdir.mkdir(parents=True, exist_ok=True)
        objects = []
        for path in sources:
            obj = objdir / (path.stem + ('.obj' if windows else '.o'))
            objects.append(obj)
            if windows:
                options = ['/I' + str(source), '/I' + str(output), '/c', path, '/Fo:' + str(obj),
                           '/Fa' + str(objdir) + '\\', '/Fd:' + str(objdir / (name + '.pdb'))]
            else:
                options = ['-I', source, '-I', output, '-c', path, '-o', obj]
            compiles.append((name + '/' + path.name, compiler + flags + options))
        exe = root / (name + ('.exe' if windows else ''))
        outputs.append(exe)
        if windows:
            command = compiler + ['/nologo'] + objects + ['/Fe:' + str(exe), '/link', 'user32.lib',
                       'ntdll.lib', '/OPT:REF', '/LTCG', '/DEBUG',
                       '/PDB:' + str(objdir / (name + '-link.pdb'))]
        else:
            static = ['-static'] if system == 'Linux' and not args.no_static else []
            command = compiler + flags + objects + static + ['-o', exe]
        links.append((name, command))
    print('\n=== Compiling {} in parallel ({} jobs) ==='.format(', '.join(TARGETS), args.jobs), flush=True)
    parallel_wave(compiles, args.jobs, root, env, args.verbose)
    print('\n=== Linking {} in parallel ==='.format(', '.join(TARGETS)), flush=True)
    parallel_wave(links, args.jobs, root, env, args.verbose)
    for exe in outputs:
        if not exe.is_file():
            raise BuildError('Build did not produce ' + str(exe))
    return outputs, env, vcvars


def build_debug_host(root, output, args, env, system, vcvars):
    cmake = shutil.which('cmake', path=env_path(env))
    if not cmake and vcvars:
        cmake = visual_studio_cmake(vcvars)
    if not cmake:
        raise BuildError('CMake is required for dcc-debug-host. Install CMake and a C++17 '
                         'compiler, or use --skip-debug-host to build only the six C tools.')
    print('\n=== Building dcc-debug-host and example I/O adapter ===', flush=True)
    build = output / 'dcc_debug_host'
    configure = [cmake, '-S', root / 'src' / 'dcc_debug_host', '-B', build,
                 '-DBUILD_TESTING=OFF', '-DDCC_DEBUG_HOST_BUILD_EXAMPLES=ON',
                 '-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=' + str(root),
                 '-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE=' + str(root)]
    if system != 'Windows':
        configure.append('-DCMAKE_BUILD_TYPE=Release')
    invoke(configure, root, env, args.verbose, 'debugger host configuration')
    invoke([cmake, '--build', build, '--config', 'Release', '--parallel', str(args.jobs),
            '--target', 'dcc-debug-host', 'dcc-debug-io-adapter-example'],
           root, env, args.verbose, 'debugger host and example adapter compilation')
    names = {'Windows': ('dcc-debug-host.exe', 'dcc-debug-io-adapter-example.dll'),
             'Darwin': ('dcc-debug-host', 'libdcc-debug-io-adapter-example.dylib'),
             'Linux': ('dcc-debug-host', 'libdcc-debug-io-adapter-example.so')}
    outputs = [root / name for name in names[system]]
    if not all(path.is_file() for path in outputs):
        raise BuildError('Debugger host artifacts were not built in the repository root.')
    return outputs


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-path', '-OutputPath', default='build', help='object/build directory (default: build)')
    parser.add_argument('--cc', '-CC', help='POSIX C compiler (default: CC, clang on macOS, gcc elsewhere)')
    parser.add_argument('--no-static', '-NoStatic', action='store_true', help='link Linux C tools dynamically')
    parser.add_argument('--verbose-commands', '-VerboseCommands', dest='verbose', action='store_true')
    parser.add_argument('--jobs', '-Jobs', type=int, default=os.environ.get('JOBS') or str(os.cpu_count() or 1))
    parser.add_argument('--skip-debug-host', action='store_true', help='build only the six C tools (no CMake needed)')
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error('--jobs must be a positive integer')
    return args


def main(argv=None):
    args = parse_args(argv)
    output = Path(args.output_path)
    if not output.is_absolute():
        output = ROOT / output
    output = output.resolve()
    system = platform.system()
    if system not in ('Windows', 'Linux', 'Darwin'):
        print('Unsupported host platform: ' + system, file=sys.stderr)
        return 1
    print('Build artifacts: {}\nCommands: {}'.format(output, ROOT), flush=True)
    try:
        outputs, env, vcvars = build_tools(ROOT, output, args, dict(os.environ), system)
        if not args.skip_debug_host:
            outputs += build_debug_host(ROOT, output, args, env, system, vcvars)
        print('\n=== Build complete ===', flush=True)
        for path in outputs:
            print('  ' + str(path))
        return 0
    except BuildError as error:
        print('ERROR: ' + str(error), file=sys.stderr, flush=True)
        return 1


if __name__ == '__main__':
    sys.exit(main())
