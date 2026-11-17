#!/usr/bin/env python3
"""Run peephole fixtures, idempotence and negative controls (Python 3.7+)."""
import argparse
from pathlib import Path
import sys
try:
    from . import runall
except ImportError:
    import runall


def positive(value):
    value = int(value)
    if value < 1:
        raise argparse.ArgumentTypeError('must be positive')
    return value


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dccpeep', '-DccPeep')
    p.add_argument('--fixture-dir', '-FixtureDir', type=Path)
    p.add_argument('--jobs', type=positive, default=1)
    args = p.parse_args(argv)
    good = runall.dccpeep_fixtures(args.jobs, args.fixture_dir, args.dccpeep)
    print('dccpeep fixtures, idempotence, aggregate-swap negative controls and long-line: '+('PASS' if good else 'FAIL'))
    return 0 if good else 1


if __name__ == '__main__':
    sys.exit(main())
