#!/usr/bin/env python3
"""Run the local pre-push validation gates and report how long each one takes.

The canonical tool build runs first. The remaining steps only read the built
tools and use their own build directories, so they then run concurrently,
up to --parallel steps at a time, longest first. Each step's output goes to its
own log file; a failing step's log tail is printed. After a failure no new
steps are started unless --keep-going is given. A timing summary is printed at
the end and written to timings.json in the output directory.

Step times measured in parallel include contention with the other steps; use
--parallel 1 to time each step on its own.

Use --list to show the steps, and --only / --skip (comma-separated step names)
to choose which ones run. This script never commits or pushes.
"""
import argparse
import concurrent.futures
import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]

# DCC_* variables that select a toolchain location are kept; other DCC_* and
# DCC/DCCMAKE controls could change compiler behavior and are removed.
PRESERVED = {'DCC_DIR', 'DCC_HOME', 'DCC_INCLUDE', 'DCC_LIB', 'DCC_RUNTIME', 'DCC_PROCESS_SCOPE'}


def positive(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError('must be a positive integer')
    return number


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--jobs', '-j', type=positive,
                   help='parallelism passed to each step (default: CPU count for the build, '
                        'and an equal share of the CPUs for each concurrent step)')
    p.add_argument('--parallel', '-p', type=positive, default=4,
                   help='how many steps run at the same time after the build (default 4)')
    p.add_argument('--timeout', type=positive, default=60, help='per-test run timeout in seconds (default 60)')
    p.add_argument('--output-directory', help='where logs, CMake builds and timings go (default build/prepush-<time>)')
    p.add_argument('--only', help='comma-separated step names to run')
    p.add_argument('--skip', help='comma-separated step names to leave out')
    p.add_argument('--keep-going', '-k', action='store_true', help='keep starting steps after a failure')
    p.add_argument('--list', action='store_true', help='list the steps and exit')
    return p.parse_args(argv)


def clean_environment(environment):
    env = {k: v for k, v in environment.items() if k.upper() in PRESERVED or
           not (k.upper() in ('DCC', 'DCCMAKE') or k.upper().startswith('DCC_'))}
    env['PATH'] = str(ROOT) + os.pathsep + env.get('PATH', '')
    return env


def select_c_compiler(system=None, environment=None):
    system = system or os.name
    environment = os.environ if environment is None else environment
    if system == 'nt':
        return environment.get('CC') or shutil.which('clang') or 'clang'
    candidates = [environment.get('CC'), 'clang', 'gcc', 'cc']
    for candidate in candidates:
        if candidate:
            resolved = shutil.which(candidate)
            if resolved:
                return resolved
    raise RuntimeError('No C compiler found; install Clang or GCC, or set CC to an installed compiler.')


def step(name, label, commands, env=None, skip_reason=None):
    return {'name': name, 'label': label, 'commands': commands, 'env': env or {},
            'skip_reason': skip_reason}


def cmake_step(name, label, out, folder, source, options, configuration, run_tests, jobs, env=None):
    directory = str(out / folder)
    commands = [['cmake', '-S', source, '-B', directory] + options,
                ['cmake', '--build', directory, '--config', configuration, '--parallel', str(jobs)]]
    if run_tests:
        commands.append(['ctest', '--test-dir', directory, '-C', configuration,
                         '--parallel', str(jobs), '--output-on-failure'])
    return step(name, label, commands, env)


def cpu_count():
    return os.cpu_count() or 1


def build_jobs(args):
    return args.jobs or cpu_count()


def step_jobs(args):
    """Concurrent steps share the CPUs so their timeouts aren't hit by oversubscription."""
    return args.jobs or max(1, cpu_count() // args.parallel)


def build_step(args):
    return step('build', 'Build canonical tools', [[sys.executable, 'scripts/build-dcc.py', '--jobs', str(build_jobs(args))]])


def parallel_steps(args, out):
    """Steps that run after the build, longest first so they start early."""
    py = sys.executable
    numeric_jobs = step_jobs(args)
    jobs = str(numeric_jobs)
    timeout = str(args.timeout)
    cc = select_c_compiler()
    if os.name == 'nt':
        clang = shutil.which('clang')
        ninja = shutil.which('ninja')
        if clang and ninja:
            sanitizer = cmake_step(
                'mir-host-sanitize', 'MIR host tests with Clang ASan/UBSan', out,
                'mir-host-sanitize', 'src/dcc',
                ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Debug', '-DDCC_BUILD_MIR_TESTS=ON',
                 '-DCMAKE_C_COMPILER=' + clang,
                 '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer',
                 '-DDCC_RUNTIME_OUTPUT_DIRECTORY=' + str(out / 'mir-host-sanitize' / 'bin')],
                'Debug', True, numeric_jobs,
                {'ASAN_OPTIONS': 'detect_leaks=0', 'UBSAN_OPTIONS': 'halt_on_error=1'})
        else:
            sanitizer = step(
                'mir-host-sanitize', 'MIR host sanitizer tests unavailable', [],
                skip_reason='Windows MSVC does not support this ASan/UBSan configuration; '
                            'install Clang and Ninja to run it.')
    else:
        sanitizer_flags = '-fsanitize=address,undefined -fno-omit-frame-pointer'
        sanitizer_link_options = []
        if sys.platform.startswith('linux'):
            # Older Clang ASan runtimes can crash before main in PIE executables
            # on Linux/WSL. Keep instrumentation, but avoid that startup failure.
            sanitizer_flags += ' -fno-pie'
            sanitizer_link_options = ['-DCMAKE_EXE_LINKER_FLAGS=-no-pie']
        sanitizer = cmake_step(
            'mir-host-sanitize', 'MIR host tests with ASan/UBSan', out,
            'mir-host-sanitize', 'src/dcc',
            ['-DCMAKE_BUILD_TYPE=Debug', '-DDCC_BUILD_MIR_TESTS=ON',
             '-DCMAKE_C_COMPILER=' + cc,
             '-DCMAKE_C_FLAGS=' + sanitizer_flags,
             '-DDCC_RUNTIME_OUTPUT_DIRECTORY=' + str(out / 'mir-host-sanitize' / 'bin')]
            + sanitizer_link_options,
            'Debug', True, numeric_jobs,
            {'ASAN_OPTIONS': 'detect_leaks=0', 'UBSAN_OPTIONS': 'halt_on_error=1'})
    strict_env = {'DCC_MIR_REQUIRE_COMPLETE': '1', 'DCC_MIR_REQUIRE_EMIT': '1'}
    runall = [py, 'scripts/runall.py', '--mode', 'full', '--extended', '--timeout', timeout,
              '--failures-only', '--throttle-limit', jobs]
    return [
        step('script-tests', 'Python script tests',
             [[py, '-m', 'unittest', 'discover', '-s', 'scripts/tests', '-p', 'test_*.py']], {'CC': cc}),
        sanitizer,
        step('runall-stack', 'Strict full+extended suite, stack check', [runall], strict_env),
        step('runall-no-stack', 'Strict full+extended suite, no stack check', [runall + ['--no-stack-check']], strict_env),
        cmake_step('mir-host', 'MIR host tests', out, 'mir-host', 'src/dcc',
                   ['-DCMAKE_BUILD_TYPE=Debug', '-DDCC_BUILD_MIR_TESTS=ON',
                    '-DDCC_RUNTIME_OUTPUT_DIRECTORY=' + str(out / 'mir-host' / 'bin')],
                   'Debug', True, numeric_jobs),
        step('host-baselines', 'Native compiler baseline validation',
             [[py, 'scripts/validate-unit-test.py', '--jobs', jobs, '--timeout', timeout]]),
        cmake_step('cmake-release', 'Release CMake build', out, 'cmake-release', 'src/dcc',
                   ['-DCMAKE_BUILD_TYPE=Release', '-DDCC_RUNTIME_OUTPUT_DIRECTORY=' + str(out / 'cmake-release' / 'bin')],
                   'Release', False, numeric_jobs),
        cmake_step('debug-host', 'Debugger host tests', out, 'debug-host', 'src/dcc_debug_host',
                   ['-DBUILD_TESTING=ON', '-DCMAKE_BUILD_TYPE=Debug'], 'Debug', True, numeric_jobs),
        step('runtime-coverage-audit', 'Runtime coverage audit', [[py, 'scripts/audit-runtime-coverage.py']]),
        step('dccpeep', 'dccpeep tests', [[py, 'scripts/run-dccpeep-tests.py']]),
        step('mir-census', 'Extended generated-MIR census',
             [[py, 'scripts/mir-extended-census.py', '--mode', 'both', '--require-complete',
               '--jobs', jobs, '--output', str(out / 'mir-extended.tsv')]]),
        step('build-diagnostics', 'Build-diagnostic regression checks',
             [[py, '-m', 'unittest', 'scripts.tests.test_runall_build_diagnostics']]),
        step('fuzz-generator', 'MIR fuzz generator test', [[py, 'scripts/test-mir-fuzz-source.py']]),
        step('iy-safety', 'IY runtime safety audit', [[py, 'scripts/rtl-iy-safety.py']]),
    ]


def select(all_steps, only, skip):
    names = [s['name'] for s in all_steps]
    chosen = set(names)
    for option, value in (('--only', only), ('--skip', skip)):
        if not value:
            continue
        given = [v.strip() for v in value.split(',') if v.strip()]
        unknown = [v for v in given if v not in names]
        if unknown:
            raise SystemExit('{}: unknown step(s): {}. Use --list to see the steps.'.format(option, ', '.join(unknown)))
        chosen = chosen & set(given) if option == '--only' else chosen - set(given)
    return [s for s in all_steps if s['name'] in chosen]


def duration(seconds):
    seconds = int(round(seconds))
    hours, rest = divmod(seconds, 3600)
    minutes, secs = divmod(rest, 60)
    return '{}:{:02d}:{:02d}'.format(hours, minutes, secs) if hours else '{}:{:02d}'.format(minutes, secs)


def tail(path, lines=40):
    try:
        text = path.read_text(errors='replace').splitlines()
    except OSError:
        return ''
    return '\n'.join(text[-lines:])


class Runner:
    def __init__(self, base_env, logs, total):
        self.base_env = base_env
        self.logs = logs
        self.total = total
        self.lock = threading.Lock()
        self.processes = set()
        self.stopping = False

    def say(self, text):
        with self.lock:
            print(text, flush=True)

    def run(self, item, number):
        log_path = self.logs / '{:02d}-{}.log'.format(number, item['name'])
        if item.get('skip_reason'):
            log_path.write_text('SKIP: ' + item['skip_reason'] + '\n')
            self.say('  SKIP [{}/{}] {}: {}'.format(
                number, self.total, item['label'], item['skip_reason']))
            return {'name': item['name'], 'label': item['label'], 'status': 'SKIP',
                    'seconds': 0, 'log': str(log_path), 'elapsed': 0}
        env = dict(self.base_env)
        env.update(item['env'])
        self.say('  start [{}/{}] {}'.format(number, self.total, item['label']))
        started = time.monotonic()
        status = 'pass'
        with log_path.open('w') as log:
            for command in item['commands']:
                log.write('$ ' + ' '.join(command) + '\n')
                log.flush()
                if self.stopping:
                    status = 'stopped'
                    break
                try:
                    process = subprocess.Popen(command, cwd=str(ROOT), env=env, stdout=log, stderr=subprocess.STDOUT)
                except OSError as error:
                    log.write('ERROR: {}\n'.format(error))
                    status = 'FAIL'
                    break
                with self.lock:
                    self.processes.add(process)
                try:
                    code = process.wait()
                finally:
                    with self.lock:
                        self.processes.discard(process)
                if code:
                    log.write('[exit code {}]\n'.format(code))
                    status = 'stopped' if self.stopping else 'FAIL'
                    break
        elapsed = time.monotonic() - started
        message = '  {} [{}/{}] {} ({})'.format('pass' if status == 'pass' else status.upper(),
                                                number, self.total, item['label'], duration(elapsed))
        if status == 'FAIL':
            message += '\n    log: {}\n    {}'.format(log_path, tail(log_path).replace('\n', '\n    '))
        self.say(message)
        return {'name': item['name'], 'label': item['label'], 'status': status,
                'seconds': round(elapsed, 1), 'log': str(log_path), 'elapsed': elapsed}

    def stop(self):
        self.stopping = True
        with self.lock:
            for process in list(self.processes):
                process.terminate()


def run_parallel(runner, items, first_number, limit, keep_going):
    results = []
    pending = list(enumerate(items, first_number))
    failed = False
    with concurrent.futures.ThreadPoolExecutor(max_workers=limit) as pool:
        active = {}
        while pending or active:
            while pending and len(active) < limit and (keep_going or not failed):
                number, item = pending.pop(0)
                active[pool.submit(runner.run, item, number)] = number
            if not active:
                break
            done, _ = concurrent.futures.wait(active, return_when=concurrent.futures.FIRST_COMPLETED)
            for future in done:
                del active[future]
                result = future.result()
                results.append(result)
                if result['status'] == 'FAIL':
                    failed = True
    return results


def summary(results, wall_seconds):
    order = {r['name']: i for i, r in enumerate(results)}
    rows = sorted(results, key=lambda r: (-r['elapsed'], order[r['name']]))
    width = max(len(r['label']) for r in rows)
    print('\nTiming summary (wall clock {}, sum of step times {}):'.format(
        duration(wall_seconds), duration(sum(r['elapsed'] for r in results))))
    print('  {:<{w}}  {:>8}  {}'.format('step', 'time', 'result', w=width))
    for r in rows:
        print('  {:<{w}}  {:>8}  {}'.format(r['label'], duration(r['elapsed']), r['status'], w=width))


def main(argv=None):
    args = parse_args(argv)
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    out = Path(args.output_directory).resolve() if args.output_directory else ROOT / 'build' / ('prepush-' + stamp)
    chosen = select([build_step(args)] + parallel_steps(args, out), args.only, args.skip)
    if args.list:
        for item in chosen:
            print('{:<24} {}'.format(item['name'], item['label']))
        return 0
    logs = out / 'logs'
    logs.mkdir(parents=True, exist_ok=True)
    print('prepush: {} step(s), {} at a time, {} jobs for the build and {} for each other step, logs in {}'.format(
        len(chosen), args.parallel, build_jobs(args), step_jobs(args), logs), flush=True)
    runner = Runner(clean_environment(os.environ), logs, len(chosen))
    results = []
    started = time.monotonic()
    try:
        rest = [s for s in chosen if s['name'] != 'build']
        if len(rest) < len(chosen):
            results.append(runner.run(chosen[0], 1))
        if all(r['status'] == 'pass' for r in results) or args.keep_going:
            results += run_parallel(runner, rest, len(results) + 1, args.parallel, args.keep_going)
    except KeyboardInterrupt:
        runner.stop()
        print('\ninterrupted', flush=True)
    wall = time.monotonic() - started
    if results:
        summary(results, wall)
        (out / 'timings.json').write_text(json.dumps({
            'startedAt': stamp, 'wallSeconds': round(wall, 1), 'buildJobs': build_jobs(args), 'stepJobs': step_jobs(args), 'parallel': args.parallel,
            'timeout': args.timeout,
            'steps': [{k: v for k, v in r.items() if k != 'elapsed'} for r in results]}, indent=2) + '\n')
    failed = [r['name'] for r in results if r['status'] not in ('pass', 'SKIP')]
    not_run = len(chosen) - len(results)
    if failed or not_run:
        print('\nFailed: ' + (', '.join(failed) or 'none') + (' ({} step(s) not run)'.format(not_run) if not_run else ''))
        return 1
    skipped = sum(r['status'] == 'SKIP' for r in results)
    if skipped:
        print('\nAll runnable pre-push steps passed ({} step(s) skipped).'.format(skipped))
    else:
        print('\nAll pre-push steps passed.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
