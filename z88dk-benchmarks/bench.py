#!/usr/bin/env python3
"""Build and measure the CP/M benchmarks using Python 3.7 or later."""
import argparse
import concurrent.futures
import csv
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent
APPS = 'tstring sieve e tm ttt pihex mm tbig a1 adaint bint cint cobint fint forint pint'.split()
SCC_APPS = set('tstring sieve e tm ttt pihex mm tbig'.split())
FLOAT_APPS = {'pihex', 'mm'}
ARGS = {'ttt': ['10'], 'a1': ['-x', '-a:400', '6502TEST.HEX']}
for _app, _ext in [('adaint', 'ADA'), ('bint', 'BAS'), ('cint', 'C'),
                   ('cobint', 'COB'), ('fint', 'F'), ('forint', 'FOR'), ('pint', 'PAS')]:
    ARGS[_app] = ['TTT.' + _ext]


def tool(name, explicit=None, z88dk=None):
    candidates = [explicit] if explicit else []
    if name == 'zcc' and z88dk:
        candidates.append(str(Path(z88dk) / 'bin' / ('zcc.exe' if os.name == 'nt' else 'zcc')))
    candidates.append(name)
    if os.name == 'nt':
        candidates.append('C:/z88dk/bin/zcc.exe' if name == 'zcc' else 'C:/ntbin/ntvcm.exe')
    for candidate in candidates:
        found = shutil.which(candidate)
        if found:
            return str(Path(found).resolve())
        if explicit:
            break
    raise RuntimeError('Cannot find %s; put it on PATH or specify --%s.' % (name, name))


def build_command(zcc, app, compiler):
    """Keep the original m.bat/mf.bat options, including math-library selection."""
    floating = app in FLOAT_APPS
    if compiler == 'zsdcc':
        flags = ['-DSDCC', '--math32'] if floating else ['-vn', '-DSDCC', '-lm', '-lmath32']
    else:
        flags = ['-DZCC', '-lmath48'] if floating else ['-DZCC', '-lm']
    command = [zcc, '+cpm'] + flags + [
        '-pragma-define:CRT_ENABLE_COMMANDLINE=1',
        '-pragma-define:CRT_COMMANDLINE_REDIRECTION=0',
        '-pragma-define:CRT_OPEN_MAX=1']
    if compiler == 'zsdcc' and not floating:
        command += ['-pragma-define:CLIB_MATH32_IEEE=1', '-pragma-define:CLIB_32BIT_FLOATS=1']
    command.append('-DLOAD_BOOK_BIN')
    if floating:
        command.append('-pragma-include:fppragma.inc')
    elif compiler == 'sccz80':
        command.append('-lmath48')
    command += ['--list', '--c-code-in-asm', '-m']
    command += (['-compiler=sdcc', '-SO3', '--max-allocs-per-node200000']
                if compiler == 'zsdcc' else ['-O2'])
    return command + [app + '.c', '-o', app + '.com']


def measured(stdout, stderr, passes):
    text = (stdout + stderr).replace('\r', '')
    match = re.search(r'Z80\s+cycles:\s*([\d,]+)', text)
    if not match:
        raise ValueError('ntvcm did not report a Z80 cycle count')
    cycles = int(match.group(1).replace(',', ''))
    return cycles, cycles / 4000000.0 / passes


def validate(app, output):
    expected = (ROOT / 'baselines' / (app + '.txt')).read_text(encoding='utf-8').replace('\r', '').strip()
    if output.strip() == expected:
        return 'pass', 'exact expected output'
    if app == 'mm':
        lines = output.strip().splitlines()
        values = [re.fullmatch(r'summ is : ([0-9.]+)', line) for line in lines]
        sums = [465880.0, 465880.0, 180666.0 + 2.0 / 3.0, 180666.0 + 2.0 / 3.0]
        if len(values) == 4 and all(values) and all(
                abs(float(match.group(1)) - want) / want < 1e-6
                for match, want in zip(values, sums)):
            return 'pass', 'matrix sums within 1 ppm (math-library precision differs)'
    if app == 'tbig' and 'tbig FAILED: 2 bad records' in output and 'CORRUPTED' in output:
        return 'known-failure', 'past-8MB write wraps around and corrupts record zero'
    return 'failed', 'output differs from baselines/' + app + '.txt'


def execute(app, compiler, args, zcc, ntvcm):
    started = time.monotonic()
    result = {'compiler': compiler, 'app': app, 'status': 'failed'}
    if compiler == 'sccz80' and app not in SCC_APPS:
        result.update(status='unsupported', detail='current source does not compile with sccz80')
        return result
    folder = args.build_dir / compiler / app
    folder.mkdir(parents=True, exist_ok=True)
    result['directory'] = str(folder)
    try:
        for name in (app + '.c', 'benchmark_rand.h', 'benchmark_nextafter.h', 'fppragma.inc'):
            shutil.copy2(str(ROOT / name), str(folder / name))
        for name in ARGS.get(app, []):
            fixture = ROOT / 'fixtures' / name
            if fixture.is_file():
                shutil.copy2(str(fixture), str(folder / name))
        com = folder / (app + '.com')
        if com.exists():
            com.unlink()
        command = build_command(zcc, app, compiler)
        result['build_command'] = command
        env = dict(os.environ)
        env['PATH'] = str(Path(zcc).parent) + os.pathsep + env.get('PATH', '')
        if args.z88dk:
            env['ZCCCFG'] = str(Path(args.z88dk).resolve() / 'lib' / 'config')
        build = subprocess.run(command, cwd=str(folder), env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=args.build_timeout)
        (folder / 'build.log').write_bytes(build.stdout)
        if build.returncode or not com.is_file():
            result['detail'] = 'build failed; see build.log (exit %d)' % build.returncode
            return result
        raw = com.stat().st_size
        result.update(raw_bytes=raw, rounded_bytes=((raw + 127) // 128) * 128)
        if args.command == 'build':
            result.update(status='built', detail=str(com))
            return result
        command = [ntvcm, '-s:0', '-p', com.name] + ARGS.get(app, [])
        result['run_command'] = command
        run = subprocess.run(command, cwd=str(folder), stdin=subprocess.DEVNULL,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=args.run_timeout)
        (folder / 'run.stdout').write_bytes(run.stdout)
        (folder / 'run.stderr').write_bytes(run.stderr)
        stdout, stderr = run.stdout.decode('latin1'), run.stderr.decode('latin1')
        passes = 10 if app == 'ttt' else 1
        cycles, seconds = measured(stdout, stderr, passes)
        result.update(cycles=cycles, passes=passes, seconds=seconds)
        output = stdout.replace('\r', '').split('\nelapsed milliseconds:', 1)[0].strip()
        status, detail = validate(app, output)
        result.update(status=status, detail=detail)
        if run.returncode:
            result.update(status='failed', detail='emulator exit %d: %s' % (run.returncode, detail))
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        result['detail'] = str(error)
    finally:
        result['host_seconds'] = round(time.monotonic() - started, 3)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['build', 'run'])
    parser.add_argument('app', nargs='?', help='one app (default for run: all 16)')
    parser.add_argument('legacy_compiler', nargs='?', choices=['old'], type=str.lower, help='old selects sccz80')
    parser.add_argument('--compiler', choices=['zsdcc', 'sccz80', 'both'], default='zsdcc')
    parser.add_argument('--apps', help='comma-separated app names')
    parser.add_argument('--z88dk', default=os.environ.get('Z88DK'), help='z88dk installation root')
    parser.add_argument('--zcc', default=os.environ.get('ZCC_TOOL'), help='zcc executable')
    parser.add_argument('--ntvcm', default=os.environ.get('NTVCM_TOOL'), help='ntvcm executable')
    parser.add_argument('--jobs', type=int, default=1)
    parser.add_argument('--build-timeout', type=int, default=1800)
    parser.add_argument('--run-timeout', type=int, default=300)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--allow-known-failures', action='store_true', help='accept explicitly identified tbig failures')
    args = parser.parse_args(argv)
    if min(args.jobs, args.build_timeout, args.run_timeout) <= 0:
        parser.error('jobs and timeouts must be positive')
    if args.apps and args.app:
        parser.error('use either an app name or --apps')
    if args.command == 'build' and not (args.app or args.apps):
        parser.error('build requires an app name or --apps')
    apps = (args.apps.split(',') if args.apps else [args.app] if args.app else APPS)
    apps = [Path(app.strip()).stem.lower() for app in apps]
    if not apps or any(app not in APPS for app in apps):
        parser.error('apps must be selected from: ' + ', '.join(APPS))
    compiler = 'sccz80' if args.legacy_compiler else args.compiler
    compilers = ['sccz80', 'zsdcc'] if compiler == 'both' else [compiler]
    args.build_dir = args.build_dir.resolve()
    try:
        zcc = tool('zcc', args.zcc, args.z88dk)
        ntvcm = tool('ntvcm', args.ntvcm) if args.command == 'run' else None
    except RuntimeError as error:
        parser.error(str(error))
    args.build_dir.mkdir(parents=True, exist_ok=True)
    print('zsdcc allocation budget: 200000; simulated 4 MHz Z80; TTT = ten passes / ten.', flush=True)
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(execute, app, backend, args, zcc, ntvcm)
                   for app in apps for backend in compilers]
        for future in concurrent.futures.as_completed(futures):
            result = future.result()
            results.append(result)
            if 'seconds' in result:
                detail = '%.6f s, %d raw / %d rounded bytes; %s' % (
                    result['seconds'], result['raw_bytes'], result['rounded_bytes'], result['detail'])
            else:
                detail = result['detail']
            print('%-7s %-8s %-13s %s' % (
                result['compiler'], result['app'], result['status'], detail), flush=True)
    results.sort(key=lambda row: (row['compiler'], APPS.index(row['app'])))
    (args.build_dir / 'results.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    fields = ['compiler', 'app', 'status', 'cycles', 'passes', 'seconds', 'raw_bytes', 'rounded_bytes']
    with (args.build_dir / 'results.csv').open('w', newline='') as output:
        writer = csv.DictWriter(output, fieldnames=fields, extrasaction='ignore')
        writer.writeheader()
        writer.writerows(results)
    failed = [r for r in results if r['status'] == 'failed' or
              (args.command == 'build' and r['status'] == 'unsupported') or
              (r['status'] == 'known-failure' and not args.allow_known_failures)]
    print('Reports: %s; %d failure(s).' % (args.build_dir, len(failed)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
