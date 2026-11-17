#!/usr/bin/env python3
"""Run the complete local pre-push gate (Python 3.7+).

Stops at the first failed gate and returns its status; does not commit or push.
The MIR proof subtools still require PowerShell 7. The diagnostic and host
validation gates use Python. All validation coverage is retained.
Use --list to inspect the gate inventory without running the validation.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def bounded_int(low, high):
    def parse(value):
        number = int(value)
        if not low <= number <= high:
            raise argparse.ArgumentTypeError('must be between {} and {}'.format(low, high))
        return number
    return parse


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', '-Jobs', type=bounded_int(1, 1024), default=os.cpu_count() or 1)
    parser.add_argument('--mutation-jobs', '-MutationJobs', type=bounded_int(0, 1024), default=0)
    parser.add_argument('--mutation-build-jobs', '-MutationBuildJobs', type=bounded_int(1, 1024), default=4)
    parser.add_argument('--timeout', '--run-timeout', '-RunTimeout', type=bounded_int(1, 3600), default=60)
    parser.add_argument('--output-directory', '-OutputDirectory')
    parser.add_argument('--require-complete', '-RequireComplete', action='store_true')
    parser.add_argument('--list', '-List', action='store_true')
    return parser.parse_args(argv)


def gate_commands(args, pwsh):
    prefix = [pwsh, '-NoLogo', '-NoProfile', '-File']
    proof = prefix + ['scripts/run-mir-proof-suite.ps1', '-Jobs', str(args.jobs),
                       '-MutationJobs', str(args.mutation_jobs),
                       '-MutationBuildJobs', str(args.mutation_build_jobs),
                       '-RunTimeout', str(args.timeout)]
    if args.output_directory:
        proof += ['-OutputDirectory', args.output_directory]
    if args.require_complete:
        proof.append('-RequireComplete')
    if args.list:
        proof.append('-List')
    return [
        ('Build-diagnostic regression checks', [sys.executable, '-m', 'unittest', 'scripts.tests.test_runall_build_diagnostics']),
        ('Native compiler baseline validation', [sys.executable, 'scripts/validate-unit-test.py',
             '--jobs', str(args.jobs), '--timeout', str(args.timeout)]),
        ('Full MIR proof suite (Mode full, extended, stack and no-stack)', proof),
    ]


def main(argv=None):
    args = parse_args(argv)
    # Never silently omit the remaining PS-only proof gates.
    pwsh = shutil.which('pwsh')
    if not pwsh:
        print('ERROR: PowerShell 7 (pwsh) is still required by the MIR proof subtools.', file=sys.stderr)
        return 1
    try:
        for label, command in gate_commands(args, pwsh):
            print(label if args.list else 'Running ' + label, flush=True)
            if args.list and command[-1] != '-List':
                continue
            result = subprocess.run(command, cwd=str(ROOT))
            if result.returncode:
                return result.returncode
        if not args.list:
            print('Pre-push validation passed', flush=True)
        return 0
    except OSError as error:
        print('ERROR: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
