"""Focused, compiler-independent contracts for the shared process supervisor."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[2]
PWSH = shutil.which("pwsh")


@unittest.skipUnless(PWSH, "requires PowerShell")
class ProcessSupervisionTests(unittest.TestCase):
    def setUp(self):
        self.workspace = ROOT / "build" / ("process-supervision-test-" + uuid.uuid4().hex)
        self.workspace.mkdir(parents=True)

    def tearDown(self):
        shutil.rmtree(self.workspace)

    def run_powershell(self, script, environment=None):
        prefix = (
            f"Import-Module '{ROOT.as_posix()}/scripts/process-supervision.psm1'; "
            f"$directory = '{self.workspace.as_posix()}'; "
            f"$python = '{Path(sys.executable).as_posix()}'; "
        )
        completed = subprocess.run([PWSH, "-NoProfile", "-Command", prefix + script],
                                   env=environment, capture_output=True, text=True, timeout=15)
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        self.assertFalse(list(self.workspace.rglob("process-id")), "scope not cleaned")
        return json.loads(completed.stdout)

    def assert_process_gone(self, process_id):
        command = (
            f"$p = Get-Process -Id {process_id} -ErrorAction SilentlyContinue; "
            "if ($p -and -not $p.HasExited) { exit 1 }; exit 0"
        )
        deadline = time.monotonic() + 2
        while True:
            completed = subprocess.run([PWSH, "-NoProfile", "-Command", command],
                                       capture_output=True, text=True, timeout=5)
            if completed.returncode == 0:
                return
            if time.monotonic() >= deadline:
                self.fail(f"Owned descendant {process_id} survived cleanup")
            time.sleep(0.05)

    def test_environment_is_inherited_and_policy_is_explicit(self):
        caller_temp = self.workspace / "caller-temp"
        caller_temp.mkdir()
        expected = {
            "DCC": "caller-dcc", "DCC_MIR_CACHE_VERIFY": "caller-cache",
            "LLVM_PROFILE_FILE": str(self.workspace / "caller-%p.profraw"),
            "TMPDIR": str(caller_temp), "TMP": str(caller_temp), "TEMP": str(caller_temp),
        }
        code = (
            "import json, os; print(json.dumps("
            f"{{key: os.getenv(key) for key in {list(expected)!r}}}))"
        )
        result = self.run_powershell("$code = @'\n" + code + "\n'@\n" + r'''
$first = Complete-SupervisedProcess (Start-SupervisedProcess $python @("-c", $code) $directory)
$second = Complete-SupervisedProcess (Start-SupervisedProcess $python @("-c", $code) $directory `
    -Environment @{ DCC = "child-dcc"; LLVM_PROFILE_FILE = "child-%p.profraw" } `
    -RemoveEnvironment @("DCC_MIR_CACHE_VERIFY"))
if ($first.ExitCode -ne 0 -or $second.ExitCode -ne 0 -or
    $first.TimedOut -or $second.TimedOut) { throw "Environment probe failed" }
@{
    inherited = $first.Output | ConvertFrom-Json
    overridden = $second.Output | ConvertFrom-Json
    parentDcc = $env:DCC
    parentProfile = $env:LLVM_PROFILE_FILE
} | ConvertTo-Json
''', dict(os.environ, **expected))
        self.assertEqual(result["inherited"], expected)
        self.assertEqual(result["overridden"], dict(
            expected, DCC="child-dcc", LLVM_PROFILE_FILE="child-%p.profraw",
            DCC_MIR_CACHE_VERIFY=None))
        self.assertEqual(result["parentDcc"], expected["DCC"])
        self.assertEqual(result["parentProfile"], expected["LLVM_PROFILE_FILE"])

    def inherited_pipe_script(self):
        return "$code = @'\n" + (
            'import subprocess, sys; '
            'child = subprocess.Popen([sys.executable, "-c", '
            '"import time; time.sleep(4)"]); '
            'print(child.pid, flush=True)'
        ) + "\n'@\n"

    def test_completion_budget_includes_inherited_pipe_drain(self):
        result = self.run_powershell(self.inherited_pipe_script() + r'''
$child = Start-SupervisedProcess $python @("-c", $code) $directory
$startup = [Diagnostics.Stopwatch]::StartNew()
while (-not $child.Process.HasExited -and $startup.Elapsed.TotalSeconds -lt 5) {
    Start-Sleep -Milliseconds 10
}
if (-not $child.Process.HasExited -or $child.Stdout.IsCompleted) {
    throw "Fixture did not leave an inherited pipe after parent exit"
}
$watch = [Diagnostics.Stopwatch]::StartNew()
$result = Complete-SupervisedProcess $child 1
@{ timedOut = $result.TimedOut; elapsed = $watch.Elapsed.TotalSeconds;
   child = [int]$result.Output.Trim() } | ConvertTo-Json
''')
        self.assertTrue(result["timedOut"])
        self.assertLess(result["elapsed"], 2.5)
        self.assert_process_gone(result["child"])
        print(f"Shared supervisor orphan-pipe completion: {result['elapsed']:.3f}s; child gone")

    def test_post_exit_grace_preempts_long_overall_deadline(self):
        result = self.run_powershell(self.inherited_pipe_script() + r'''
$child = Start-SupervisedProcess $python @("-c", $code) $directory -DrainTimeoutSeconds 1
while (-not (Test-SupervisedProcessComplete $child 20)) { Start-Sleep -Milliseconds 10 }
$result = Complete-SupervisedProcess $child (20 - $child.Clock.Elapsed.TotalSeconds)
@{ timedOut = $result.TimedOut; elapsed = $child.Clock.Elapsed.TotalSeconds;
   child = [int]$result.Output.Trim() } | ConvertTo-Json
''')
        self.assertTrue(result["timedOut"])
        self.assertLess(result["elapsed"], 3)
        self.assert_process_gone(result["child"])

    def test_completed_process_can_be_collected_with_zero_budget(self):
        result = self.run_powershell(r'''
$child = Start-SupervisedProcess $python @("-c", "print(42)") $directory
while (-not (Test-SupervisedProcessComplete $child 5)) { Start-Sleep -Milliseconds 10 }
$result = Complete-SupervisedProcess $child 0
Stop-SupervisedProcess $child
Stop-SupervisedProcess $child
@{ exitCode = $result.ExitCode; timedOut = $result.TimedOut;
   output = $result.Output.Trim() } | ConvertTo-Json
''')
        self.assertEqual(result, {"exitCode": 0, "timedOut": False, "output": "42"})

    def test_overall_deadline_and_explicit_stop_clean_live_processes(self):
        code = (
            "import os, pathlib, time; "
            f"pathlib.Path({str(self.workspace / 'child-pid')!r}).write_text(str(os.getpid())); "
            "time.sleep(30)"
        )
        result = self.run_powershell("$code = @'\n" + code + "\n'@\n" + r'''
$child = Start-SupervisedProcess $python @("-c", $code) $directory
while (-not (Test-SupervisedProcessComplete $child 2)) { Start-Sleep -Milliseconds 10 }
$processId = [int](Get-Content -LiteralPath "$directory/child-pid")
$result = Complete-SupervisedProcess $child ([Math]::Max(0, 2 - $child.Clock.Elapsed.TotalSeconds))
$other = Start-SupervisedProcess $python @("-c", "import time; time.sleep(30)") $directory
$otherId = $other.Process.Id
Stop-SupervisedProcess $other
Stop-SupervisedProcess $other
@{ timedOut = $result.TimedOut; child = $processId; bootstrap = $otherId } | ConvertTo-Json
''')
        self.assertTrue(result["timedOut"])
        self.assert_process_gone(result["child"])
        self.assert_process_gone(result["bootstrap"])


if __name__ == "__main__":
    unittest.main()
