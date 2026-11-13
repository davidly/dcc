#!/usr/bin/env python3
"""Check the parked reproducers in tests/known-bugs/.

Each tests/known-bugs/<name>.c reproduces one entry in compiler-bugs.md and
has the output it should produce in tests/known-bugs/baselines/<name>.txt.
They are parked there, outside the suite scripts/runall.py runs, because dcc
currently fails them.

For every reproducer this script:

  1. builds it with the host compiler (the flags validate-unit-test.ps1 uses)
     and requires the output to equal the baseline, and requires a build with
     ASan and UBSan to agree, so the baseline is proven and the test defined;
  2. builds and runs it with dcc, with and without dccpeep, and reports
     whether dcc still fails it.

  run-known-bugs.py             check every reproducer
  run-known-bugs.py bug003      check some
  run-known-bugs.py --update    rewrite baselines from the host compiler

A reproducer reported FIXED passes in both modes: move it and its baseline to
tests/ and tests/baselines/, add a tests/perf_baselines.csv row, and close the
entry in compiler-bugs.md. The exit status is 1 only when a baseline is not
proven by the host compiler.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BUGS = REPO / 'tests' / 'known-bugs'
HOST_FLAGS = ['-std=gnu99', '-w', '-fsigned-char', '-fno-common']
SANITIZE = ['-O1', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def host_output(source, work, flags, timeout):
    exe = work / 'host'
    r = subprocess.run(['clang', *HOST_FLAGS, *flags, str(source), '-o', str(exe)],
                       capture_output=True, text=True)
    if r.returncode:
        return None, 'host build failed: ' + r.stderr.strip()[:200]
    try:
        r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=timeout,
                           stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        return None, 'host run timed out'
    if r.returncode:
        return None, 'host run failed: ' + r.stderr.strip()[-200:]
    return r.stdout, None


def dcc_verdict(source, work, peep, expected, args):
    build = work / ('peep' if peep else 'nopeep')
    shutil.rmtree(build, ignore_errors=True)
    cmd = [str(REPO / 'dccmake'), 'dcc-input=%s' % source, 'dcc-output=T',
           'dcc-build-dir=%s' % build, 'dcc-peep=%s' % ('true' if peep else 'false'),
           'dcc-stack-check=true', 'dcc-floatio=true', 'dcc-flongio=true',
           'ntvcm-tool=%s' % args.emulator]
    if args.dcc:
        cmd.append('dcc-tool=%s' % args.dcc.resolve())
    r = subprocess.run(cmd, cwd=REPO, stdin=subprocess.DEVNULL, capture_output=True, text=True)
    log = r.stdout + r.stderr
    notes = [ln.strip() for ln in log.splitlines()
             if not ln.startswith('+')
             and ('error DCC' in ln or 'dcc: fatal' in ln or ln.startswith('; MIR '))]
    if r.returncode:
        text = notes[0] if notes else 'build failed'
        return 'does not compile: ' + text.replace(str(source) + ':', 'line ')[:110]
    try:
        run = subprocess.run([args.emulator, '-s:0', 'T.COM'], cwd=build,
                             stdin=subprocess.DEVNULL, capture_output=True, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        return 'hangs (over %d s)' % args.timeout
    output = run.stdout.decode('latin-1').replace('\r', '')
    if output == expected:
        return 'pass'
    want, got = expected.splitlines(), output.splitlines()
    wrong = sum(1 for w, g in zip(want, got) if w != g) + abs(len(want) - len(got))
    note = '; dcc reported: ' + notes[0][:60] if notes else ''
    return 'wrong output (%d of %d lines)%s' % (wrong, len(want), note)


def check(name, args):
    source = BUGS / (name + '.c')
    baseline = BUGS / 'baselines' / (name + '.txt')
    work = args.work / name
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    out, err = host_output(source, work, ['-O2'], args.timeout)
    if out is not None:
        # unsequenced modifications and missing returns are undefined but
        # invisible to the sanitizers; clang diagnoses them at compile time
        lint = subprocess.run(['clang', '-std=gnu99', '-fsigned-char', '-fsyntax-only',
                               '-Wno-everything', '-Werror=unsequenced', '-Werror=return-type',
                               str(source)], capture_output=True, text=True)
        if lint.returncode:
            out, err = None, 'not well defined: ' + lint.stderr.strip().splitlines()[0][-150:]
    if out is not None:
        sanitized, err = host_output(source, work, SANITIZE, args.timeout)
        if sanitized is not None and sanitized != out:
            out, err = None, 'sanitized host build prints different output'
        elif sanitized is None:
            out = None
    if out is None:
        return name, 'UNPROVEN', err, []
    if args.update:
        baseline.parent.mkdir(exist_ok=True)
        baseline.write_text(out)
    if not baseline.exists():
        return name, 'UNPROVEN', 'no baseline (run with --update)', []
    if baseline.read_text() != out:
        return name, 'UNPROVEN', 'baseline differs from the host compiler output', []
    verdicts = [dcc_verdict(source, work, peep, out, args) for peep in (False, True)]
    shutil.rmtree(work, ignore_errors=True)
    if all(v == 'pass' for v in verdicts):
        return name, 'FIXED', '', verdicts
    return name, 'fails', '', verdicts


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('names', nargs='*', help='reproducers to check (default: all)')
    parser.add_argument('--update', action='store_true',
                        help='rewrite baselines from the host compiler output')
    parser.add_argument('--emulator', default='ntvcm')
    parser.add_argument('--dcc', type=Path,
                        help='another dcc build to check instead of the one in the repo root')
    parser.add_argument('--timeout', type=int, default=20, help='seconds to run a program')
    parser.add_argument('--work', type=Path, default=REPO / 'build' / 'known-bugs')
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()
    names = args.names or sorted(p.stem for p in BUGS.glob('*.c'))
    missing = [n for n in names if not (BUGS / (n + '.c')).exists()]
    if missing:
        parser.error('no such reproducer: ' + ', '.join(missing))
    counts = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for name, status, note, verdicts in pool.map(lambda n: check(n, args), names):
            counts[status] = counts.get(status, 0) + 1
            if status == 'UNPROVEN':
                print('%-10s UNPROVEN  %s' % (name, note))
            elif status == 'FIXED':
                print('%-10s FIXED     passes with and without dccpeep' % name)
            elif verdicts[0] == verdicts[1]:
                print('%-10s fails     %s' % (name, verdicts[0]))
            else:
                print('%-10s fails     nopeep: %s; peep: %s' % (name, verdicts[0], verdicts[1]))
    print('%d reproducers: %s' % (len(names), ', '.join(
        '%d %s' % (n, s) for s, n in sorted(counts.items()))))
    return 1 if counts.get('UNPROVEN') else 0


if __name__ == '__main__':
    sys.exit(main())
