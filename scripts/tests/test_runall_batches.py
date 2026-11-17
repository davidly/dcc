"""Batch coverage, bounded concurrency, and failure/metric propagation."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

from scripts import runall


class Batches(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        (self.directory / 'TDVBYTE.COM').write_bytes(b'fake COM')
        self.args = argparse.Namespace(emulator='ntvcm', timeout=1)
        self.baseline = runall.load_baseline(Path(__file__).resolve().parents[2] /
                                            'tests/baselines/tdvbyte_batch.txt')

    def result(self, index, count):
        return True, [], {'cycles': 100 + index, 'size': 8}

    def invoke(self, side_effect=None):
        def checked(name, directory, baseline, arguments, stdin, args, **kwargs):
            index, count = map(int, arguments.split())
            self.assertEqual(count, 7)
            self.assertEqual(kwargs['expected_code'], 0)
            return (side_effect or self.result)(index, count)
        with patch.object(runall, 'check_run', side_effect=checked) as check:
            result = runall.check_batches('tdvbyte', self.directory, self.baseline,
                                         7, 2, self.args)
        self.assertEqual(sorted(int(c[0][3].split()[0]) for c in check.call_args_list), list(range(7)))
        return result

    def test_all_batches_and_summed_cycles(self):
        ok, lines, metrics = self.invoke()
        self.assertTrue(ok)
        self.assertEqual(lines, [])
        self.assertEqual(metrics, {'cycles': 721, 'size': 8})

    def test_any_failure_propagates_with_batch_identity(self):
        def one(index, count):
            return index != 3, ['timed out'] if index == 3 else [], {'cycles': 100, 'size': 8}
        ok, lines, _ = self.invoke(one)
        self.assertFalse(ok)
        self.assertIn('[batch 4/7]', lines[0])

    def test_missing_cycle_count_is_an_error(self):
        def one(index, count):
            return True, [], {'cycles': None if index == 6 else 100, 'size': 8}
        ok, lines, metrics = self.invoke(one)
        self.assertFalse(ok)
        self.assertIsNone(metrics['cycles'])
        self.assertIn('missing cycle count', lines[0])

    def test_divisor_partition_has_no_gaps_or_duplicates(self):
        batches = [list(range(index+1, 267, 7)) for index in range(7)]
        self.assertEqual([len(batch) for batch in batches], [38]*7)
        self.assertEqual(sorted(n for batch in batches for n in batch), list(range(1, 267)))
        self.assertEqual(sum(len(batch)*65536 for batch in batches), 17432576)

    def test_child_reservations_respect_process_limit(self):
        for limit in (1, 2, 3, 4, 8, 16, 32):
            for modes in (1, 2):
                children = runall.batch_workers(limit, modes, 7)
                outer = max(1, limit - modes*(children-1))
                active_batched = min(outer, modes)
                self.assertLessEqual(outer + active_batched*(children-1), limit)

    def test_weighted_queue_releases_slots_for_later_jobs(self):
        lock = threading.Lock()
        release = threading.Event()
        active, peak = 0, 0
        def job(name, weight):
            nonlocal active, peak
            with lock:
                active += weight
                peak = max(peak, active)
                if active == 3:
                    release.set()
            self.assertTrue(release.wait(5))
            with lock:
                active -= weight
            return runall.Result(name, True, .02, [])
        items = [runall.WorkItem(job, (str(i), weight), str(i), weight)
                 for i, weight in enumerate((2, 1, 1, 1, 1, 1))]
        result = runall.execute(items, 3, True)
        self.assertEqual(len(result.results), len(items))
        self.assertEqual(result.not_started, [])
        self.assertEqual(peak, 3)
        self.assertEqual(active, 0)
