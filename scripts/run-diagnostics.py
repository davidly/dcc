#!/usr/bin/env python3
"""Validate or explicitly refresh compiler diagnostic baselines (Python 3.7+)."""
try:
    from .diagnostics import main
except ImportError:
    from diagnostics import main
if __name__=='__main__':raise SystemExit(main())
