#!/usr/bin/env python3
"""Run the complete local pre-push gate (Python 3.7+).

Stops at the first failed gate and returns its status; does not commit or push.
All entry points use Python. The final coverage/clobber subtools still require
PowerShell 7 and sh until those dependencies are migrated. Coverage is retained.
Use --list to inspect the gate inventory without running the validation.
"""
import argparse
import os
from pathlib import Path
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


def gate_commands(args):
    proof = [sys.executable, 'scripts/run-mir-proof-suite.py', '--jobs', str(args.jobs),
                       '--mutation-jobs', str(args.mutation_jobs),
                       '--mutation-build-jobs', str(args.mutation_build_jobs),
                       '--timeout', str(args.timeout)]
    if args.output_directory:
        proof += ['--output-directory', args.output_directory]
    if args.require_complete:
        proof.append('--require-complete')
    if args.list:
        proof.append('--list')
    return [
        ('Build-diagnostic regression checks', [sys.executable, '-m', 'unittest', 'scripts.tests.test_runall_build_diagnostics']),
        ('Native compiler baseline validation', [sys.executable, 'scripts/validate-unit-test.py',
             '--jobs', str(args.jobs), '--timeout', str(args.timeout)]),
        ('Full MIR proof suite (Mode full, extended, stack and no-stack)', proof),
    ]


def main(argv=None):
    args = parse_args(argv)
    try:
        for index, (label, command) in enumerate(gate_commands(args)):
            print(label if args.list else 'Running ' + label, flush=True)
            if args.list and index < 2:
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
