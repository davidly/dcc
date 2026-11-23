"""Bound host optimizer stalls without losing independent oracle validation."""
from pathlib import Path
import runpy
import subprocess
import sys
import unittest
from unittest import mock


class HostCompilerTimeoutTests(unittest.TestCase):
    def setUp(self):
        driver = Path(__file__).resolve().parents[1] / 'clang-oracle-fuzz.py'
        with mock.patch.object(sys, 'path', sys.path.copy()):
            self.build = runpy.run_path(str(driver))['host_build']

    def test_o2_timeout_retries_with_unrolling_disabled(self):
        result = subprocess.CompletedProcess([], 0, '', '')
        with mock.patch.object(subprocess, 'run', side_effect=(
                subprocess.TimeoutExpired('clang', 3), result)) as run:
            self.assertEqual(self.build(Path('t.c'), Path('t.host'), ['-O2'], 3),
                             (True, ''))
        original, retry = run.call_args_list
        self.assertEqual(retry.args[0], original.args[0] + ['-fno-unroll-loops'])
        self.assertEqual(retry.kwargs, original.kwargs)
        self.assertEqual(retry.kwargs['timeout'], 3)

    def test_other_build_timeouts_are_controlled_failures(self):
        for flags in (['-O0'], ['-O1', '-fsanitize=address,undefined'],
                      ['-O2', '-fno-unroll-loops']):
            with self.subTest(flags=flags), mock.patch.object(
                    subprocess, 'run', side_effect=subprocess.TimeoutExpired('clang', 3)) as run:
                ok, reason = self.build(Path('t.c'), Path('t.host'), flags, 3)
                self.assertFalse(ok)
                self.assertIn('timed out', reason)
                self.assertEqual(run.call_count, 1)

    def test_retry_is_bounded_and_preserves_compiler_errors(self):
        for second, expected in (
                (subprocess.TimeoutExpired('clang', 3), 'loop unrolling disabled'),
                (subprocess.CompletedProcess([], 1, '', 'bad source'), 'bad source')):
            with self.subTest(second=second), mock.patch.object(
                    subprocess, 'run', side_effect=(subprocess.TimeoutExpired('clang', 3), second)):
                ok, reason = self.build(Path('t.c'), Path('t.host'), ['-O2'], 3)
                self.assertFalse(ok)
                self.assertIn(expected, reason)

    def test_normal_o2_success_does_not_disable_unrolling(self):
        with mock.patch.object(subprocess, 'run', return_value=
                subprocess.CompletedProcess([], 0, '', '')) as run:
            self.assertEqual(self.build(Path('t.c'), Path('t.host'), ['-O2']), (True, ''))
        self.assertEqual(run.call_count, 1)
        self.assertNotIn('-fno-unroll-loops', run.call_args.args[0])
