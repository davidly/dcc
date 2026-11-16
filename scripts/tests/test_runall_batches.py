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
        self.assertEqual(sorted(int(c.args[3].split()[0]) for c in check.call_args_list), list(range(7)))
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


@unittest.skipUnless(shutil.which('pwsh'), 'PowerShell 7 is unavailable')
class PowerShellBatches(unittest.TestCase):
    def test_real_processes_success_failure_missing_metrics_and_timeout(self):
        # Extract definitions without executing the top-level suite. Fake
        # emulators exercise the real asynchronous reads and cleanup paths.
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            fake = directory/'emulator.py'
            fake.write_text('''import sys,time
mode=sys.argv[1]
index=int(sys.argv[-2])
if mode=='timeout' and index==1: time.sleep(10)
print('bad' if mode=='mismatch' and index==1 else 'ok')
print('\\nelapsed milliseconds: 1')
if mode!='missing' or index!=1: print('Z80 cycles:', 100+index)
print('clock rate: unbounded')
sys.exit(17 if mode=='exit' and index==1 else 0)
''')
            ps = directory/'test.ps1'
            source = Path(__file__).resolve().parents[1]/'runall.ps1'
            def quoted(path):
                return "'"+str(path).replace("'", "''")+"'"
            ps.write_text(f'''$ErrorActionPreference='Stop'
$ast=[System.Management.Automation.Language.Parser]::ParseFile({quoted(source)},[ref]$null,[ref]$null)
$names=@('Start-ComRun','Invoke-ComRunAndCompare','Invoke-ComBatches','Stop-ProcessTree','Test-MatchesBaseline')
foreach($fn in $ast.FindAll({{param($a) $a -is [System.Management.Automation.Language.FunctionDefinitionAst]}},$true)){{
 if($fn.Name -in $names){{Invoke-Expression $fn.Extent.Text}}
}}
$results=@()
foreach($mode in @('ok','mismatch','exit','missing','timeout')){{
 $lines=[System.Collections.Generic.List[string]]::new()
 $one=Invoke-ComBatches -BuildDir {quoted(directory)} -ComFileName TDVBYTE.COM -Emulator {quoted(sys.executable)} `
  -EmulatorRunArgs @({quoted(fake)},$mode,'-p') -Count 3 -Workers 2 `
  -Baseline ([pscustomobject]@{{HasBaseline=$true;Expected='ok';Path='fake.txt'}}) `
  -Placeholders @{{}} -Lines $lines -RunTimeout 1
 $results+=@{{Mode=$mode;Passed=$one.Passed;Cycles=$one.Cycles;Lines=@($lines.ToArray())}}
}}
ConvertTo-Json -Depth 6 -InputObject @($results)
''')
            result = subprocess.run(['pwsh','-NoProfile','-File',str(ps)], capture_output=True,
                                    text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            cases = json.loads(result.stdout)
            self.assertTrue(cases[0]['Passed'], cases[0])
            self.assertEqual(int(cases[0]['Cycles']), 303)
            for case in cases[1:]:
                self.assertFalse(case['Passed'], case)
                self.assertTrue(case['Lines'], case)


if __name__ == '__main__':
    unittest.main()
