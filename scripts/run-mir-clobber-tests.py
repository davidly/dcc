#!/usr/bin/env python3
"""Run MIR emission-clobber regressions using Python 3.7+."""
try:
    from .mir_clobber import main
except ImportError:
    from mir_clobber import main
if __name__=='__main__':
    raise SystemExit(main())
