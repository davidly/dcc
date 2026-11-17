"""Exercise pre-push orchestration without launching expensive proof campaigns."""
import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('prepush', ROOT/'scripts/prepush.py')
prepush = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepush)


class PrepushTests(unittest.TestCase):
    def run_gate(self, argv=(), codes=(0, 0, 0)):
        with mock.patch.object(prepush.shutil, 'which', return_value='/tools with spaces/pwsh'), \
             mock.patch.object(prepush.subprocess, 'run', side_effect=[subprocess.CompletedProcess([], code) for code in codes]) as run:
            status = prepush.main(argv)
        return status, run

    def test_runs_all_gates_from_repo_and_forwards_options(self):
        status, run = self.run_gate(['--jobs', '3', '-MutationJobs', '2',
            '--mutation-build-jobs', '1', '--timeout', '90',
            '--output-directory', 'build/with spaces', '--require-complete'])
        self.assertEqual(status, 0)
        commands = [call[0][0] for call in run.call_args_list]
        import sys
        self.assertEqual(commands[0], [sys.executable,'-m','unittest','scripts.tests.test_runall_build_diagnostics'])
        self.assertEqual(commands[1], [sys.executable,'scripts/validate-unit-test.py','--jobs','3','--timeout','90'])
        self.assertEqual(commands[2][5:], ['-Jobs', '3', '-MutationJobs', '2',
            '-MutationBuildJobs', '1', '-RunTimeout', '90', '-OutputDirectory',
            'build/with spaces', '-RequireComplete'])
        for call in run.call_args_list:
            self.assertEqual(call[1]['cwd'], str(ROOT))
        self.assertEqual(commands[2][:4], ['/tools with spaces/pwsh', '-NoLogo', '-NoProfile', '-File'])

    def test_stops_at_each_failed_gate_and_propagates_exit(self):
        for index in range(3):
            with self.subTest(index=index):
                status, run = self.run_gate(codes=[0]*index + [7+index])
                self.assertEqual(status, 7+index)
                self.assertEqual(run.call_count, index+1)

    def test_list_only_invokes_proof_inventory(self):
        status, run = self.run_gate(['-List'])
        self.assertEqual(status, 0)
        self.assertEqual(run.call_count, 1)
        command = run.call_args[0][0]
        self.assertIn('scripts/run-mir-proof-suite.ps1', command)
        self.assertEqual(command[-1], '-List')

    def test_missing_powershell_and_launch_failure_are_reported(self):
        with mock.patch.object(prepush.shutil, 'which', return_value=None), \
             mock.patch.object(prepush.subprocess, 'run') as run:
            self.assertEqual(prepush.main([]), 1)
            run.assert_not_called()
        with mock.patch.object(prepush.shutil, 'which', return_value='pwsh'), \
             mock.patch.object(prepush.subprocess, 'run', side_effect=OSError('launch failed')):
            self.assertEqual(prepush.main([]), 1)

    @unittest.skipUnless(prepush.shutil.which('pwsh'), 'PowerShell required by proof inventory')
    def test_real_inventory_without_running_gates(self):
        import sys
        result = subprocess.run([sys.executable, str(ROOT/'scripts/prepush.py'), '--list'],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for name in ('strict stack release suite', 'strict no-stack release suite',
                     'debugger-host tests', 'instrumented compiler coverage'):
            self.assertIn(name, result.stdout)
        self.assertNotIn('Pre-push validation passed', result.stdout)

    def test_ranges_and_defaults(self):
        self.assertEqual(prepush.parse_args([]).timeout, 60)
        for argv in (['--jobs','0'], ['--mutation-jobs','-1'], ['--mutation-build-jobs','1025'], ['--timeout','3601']):
            with self.subTest(argv=argv), self.assertRaises(SystemExit):
                prepush.parse_args(argv)


if __name__ == '__main__':
    unittest.main()
