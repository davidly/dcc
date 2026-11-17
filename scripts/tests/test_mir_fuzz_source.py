"""Stable source/oracle snapshots and coverage contracts for the fuzz generator."""
import hashlib
import importlib.util
from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('mir_fuzz_source',ROOT/'scripts/new-mir-fuzz-source.py')
fuzz=importlib.util.module_from_spec(spec);spec.loader.exec_module(fuzz)


class FuzzSourceTests(unittest.TestCase):
    def test_matches_original_powershell_sources_and_oracles(self):
        # Captured from the previous generator; LF-normalized on both hosts.
        for seed,programs,expected in [
            (23117,12,'548a2db49829645dcaa170a6adce203fed1e2465f9c21677d7078d10cc170d40'),
            (0,1,'034310494f4844c92dc869c905e570db94b3516e8c46fe5718371b61b9408792'),
            (1,32,'d09aef7e84406085eabb5574d6c84dae48c02c98dceca7f23f2f04216b24bef1'),
            (65535,12,'26cacd7b71a72071618f800ffd2865b4fbe8d4d4cb7c32ada794f32b943f4fb7')]:
            with self.subTest(seed=seed,programs=programs):
                self.assertEqual(hashlib.sha256(fuzz.generate(seed,programs).encode('ascii')).hexdigest(),expected)

    def test_replay_and_conditional_callbacks(self):
        source=fuzz.generate()
        self.assertEqual(source,fuzz.generate())
        self.assertNotEqual(source,fuzz.generate(1))
        self.assertEqual(len(re.findall(r'unsigned int fuzz\d+\(unsigned int seed\)',source)),12)
        self.assertEqual(source.count('(*callback)'),12)
        for text in ('? touch8 : alter8','? touch16 : alter16','expected[96]','seed=23117'):
            self.assertIn(text,source)
        one=fuzz.generate(programs=1)
        self.assertIn('expected[8]',one)
        self.assertEqual(len(re.findall(r'unsigned int fuzz\d+\(unsigned int seed\)',one)),1)

    def test_ranges_reject_invalid_replay_controls(self):
        for seed,count in [(-1,12),(65536,12),(1,0),(1,33)]:
            with self.assertRaises(ValueError):fuzz.generate(seed,count)


if __name__=='__main__':unittest.main()
