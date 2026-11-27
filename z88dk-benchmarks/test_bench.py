"""Reporting checks that do not require z88dk or ntvcm."""
import unittest
from unittest import mock
import tempfile
import contextlib
import io
import bench


class ReportingTests(unittest.TestCase):
    def test_explicit_unsupported_build_returns_failure(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(
                bench, 'tool', return_value='zcc'), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(bench.main(['build', 'a1', 'old', '--build-dir', directory]), 1)

    def test_ttt_reports_one_pass_from_ten_pass_cycles(self):
        cycles, seconds = bench.measured('', '\r\nZ80  cycles: 78,804,599\r\n', 10)
        self.assertEqual(cycles, 78804599)
        self.assertAlmostEqual(seconds, 1.970114975)

    def test_missing_cycle_count_is_not_a_zero_time_result(self):
        with self.assertRaises(ValueError):
            bench.measured('could not load command file', '', 1)

    def test_tbig_corruption_is_never_a_pass(self):
        status, _ = bench.validate('tbig', 'record 0 CORRUPTED\ntbig FAILED: 2 bad records\n')
        self.assertEqual(status, 'known-failure')
        status, _ = bench.validate('tbig', 'tbig FAILED: 3 bad records\n')
        self.assertEqual(status, 'failed')

    def test_matrix_precision_tolerance_does_not_accept_wrong_answers(self):
        for value, wanted in [('180666.781250', 'pass'), ('180700.0', 'failed')]:
            output = 'summ is : 465880.000000\n' * 2 + ('summ is : ' + value + '\n') * 2
            self.assertEqual(bench.validate('mm', output)[0], wanted)

    def test_incomplete_output_is_not_a_pass(self):
        expected = (bench.ROOT / 'baselines' / 'tstring.txt').read_text()
        self.assertEqual(bench.validate('tstring', expected)[0], 'pass')
        self.assertEqual(bench.validate('tstring', expected.splitlines()[0])[0], 'failed')

    def test_float_and_integer_profiles_keep_original_options(self):
        integer = bench.build_command('zcc', 'sieve', 'zsdcc')
        floating = bench.build_command('zcc', 'pihex', 'zsdcc')
        self.assertIn('--max-allocs-per-node200000', integer)
        self.assertIn('--max-allocs-per-node200000', floating)
        self.assertIn('-lmath32', integer)
        self.assertIn('--math32', floating)
        self.assertIn('-pragma-include:fppragma.inc', floating)
        self.assertNotIn('-pragma-include:fppragma.inc', integer)
        self.assertIn('-O2', bench.build_command('zcc', 'sieve', 'sccz80'))


if __name__ == '__main__':
    unittest.main()
