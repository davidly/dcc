"""Exercise pre-push orchestration without launching expensive proof campaigns."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PWSH = shutil.which("pwsh")


@unittest.skipUnless(PWSH, "PowerShell 7 is required")
class PrepushTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="dcc prepush ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        scripts = self.root / "scripts"
        (scripts / "tests").mkdir(parents=True)
        shutil.copyfile(ROOT / "scripts/prepush.ps1", scripts / "prepush.ps1")
        (scripts / "tests/test_runall_build_diagnostics.ps1").write_text(
            "Add-Content -LiteralPath $env:PREPUSH_TRACE 'diagnostics'\n"
            "exit ([int]$env:PREPUSH_DIAGNOSTIC_EXIT)\n"
        )
        (scripts / "run-mir-proof-suite.ps1").write_text(
            "param($Jobs, $MutationJobs, $MutationBuildJobs, $RunTimeout, "
            "$OutputDirectory, [switch]$RequireComplete, [switch]$List)\n"
            "Add-Content -LiteralPath $env:PREPUSH_TRACE 'proof'\n"
            "@{ parameters = $PSBoundParameters; cwd = (Get-Location).Path } | "
            "ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $env:PREPUSH_RESULT\n"
            "exit ([int]$env:PREPUSH_PROOF_EXIT)\n"
        )

    def run_gate(self, *arguments, diagnostic_exit=0, proof_exit=0):
        return subprocess.run(
            [PWSH, "-NoLogo", "-NoProfile", "-File",
             str(self.root / "scripts/prepush.ps1"), *arguments],
            cwd=self.root.parent,
            env=dict(os.environ, PREPUSH_TRACE=str(self.root / "trace"),
                     PREPUSH_RESULT=str(self.root / "result.json"),
                     PREPUSH_DIAGNOSTIC_EXIT=str(diagnostic_exit),
                     PREPUSH_PROOF_EXIT=str(proof_exit)),
            capture_output=True, text=True, timeout=30,
        )

    def test_runs_both_gates_from_repo_and_forwards_options(self):
        result = self.run_gate(
            "-Jobs", "3", "-MutationJobs", "2", "-MutationBuildJobs", "1",
            "-RunTimeout", "90", "-OutputDirectory", "build/with spaces",
            "-RequireComplete",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / "trace").read_text().splitlines(),
                         ["diagnostics", "proof"])
        recorded = json.loads((self.root / "result.json").read_text())
        self.assertEqual(Path(recorded["cwd"]), self.root)
        self.assertEqual(recorded["parameters"], {
            "Jobs": "3", "MutationJobs": "2", "MutationBuildJobs": "1",
            "RunTimeout": "90", "OutputDirectory": "build/with spaces",
            "RequireComplete": {"IsPresent": True},
        })
        self.assertIn("Pre-push validation passed", result.stdout)

    def test_diagnostic_failure_stops_before_proof_suite(self):
        result = self.run_gate(diagnostic_exit=7)
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        self.assertEqual((self.root / "trace").read_text().splitlines(), ["diagnostics"])
        self.assertFalse((self.root / "result.json").exists())
        self.assertNotIn("Pre-push validation passed", result.stdout)

    def test_proof_failure_is_propagated(self):
        result = self.run_gate(proof_exit=9)
        self.assertEqual(result.returncode, 9, result.stdout + result.stderr)
        self.assertEqual((self.root / "trace").read_text().splitlines(),
                         ["diagnostics", "proof"])
        self.assertNotIn("Pre-push validation passed", result.stdout)

    def test_list_shows_real_inventory_without_running_gates(self):
        result = subprocess.run(
            [PWSH, "-NoLogo", "-NoProfile", "-File",
             str(ROOT / "scripts/prepush.ps1"), "-List"],
            cwd=self.root, capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for text in ("Build-diagnostic regression checks", "Mode full",
                     "strict stack release suite", "strict no-stack release suite",
                     "debugger-host tests", "instrumented compiler coverage"):
            self.assertIn(text, result.stdout)
        self.assertNotIn("Pre-push validation passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
