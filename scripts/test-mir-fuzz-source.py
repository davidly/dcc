#!/usr/bin/env python3
"""Run deterministic MIR fuzz source/oracle checks with Python 3.7+."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from scripts.tests.test_mir_fuzz_source import FuzzSourceTests
if __name__=='__main__':unittest.main()
