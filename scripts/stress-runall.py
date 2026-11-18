#!/usr/bin/env python3
try:from .stress_runall import main
except ImportError:from stress_runall import main
if __name__=='__main__':raise SystemExit(main())
