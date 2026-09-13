"""Verify the aggregate AST/MIR proof runner's public gate inventory."""

from pathlib import Path
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
PWSH = shutil.which("pwsh")


@unittest.skipUnless(PWSH, "PowerShell 7 is required")
class MirProofSuiteTests(unittest.TestCase):
    def test_cmake_builds_do_not_replace_the_canonical_compiler(self):
        runner = (ROOT / "scripts/run-mir-proof-suite.ps1").read_text(
            encoding="utf-8"
        )

        self.assertEqual(runner.count("-DDCC_RUNTIME_OUTPUT_DIRECTORY="), 3)
        self.assertIn('"-DCMAKE_C_COMPILER=$coverageCompiler"', runner)

    def test_runner_sanitizes_ambient_controls_and_bounds_parallel_groups(self):
        runner = (ROOT / "scripts/run-mir-proof-suite.ps1").read_text(
            encoding="utf-8"
        )

        self.assertIn("Clear-AmbientProofControls", runner)
        self.assertIn(
            "[System.StringComparison]::OrdinalIgnoreCase",
            runner,
        )
        self.assertIn(
            ") -MaxConcurrency ([Math]::Min(6, $Jobs))",
            runner,
        )
        self.assertIn(
            ") -MaxConcurrency ([Math]::Min(2, $Jobs))",
            runner,
        )
        self.assertIn(
            "[Math]::Floor($Jobs / [Math]::Min(4, $Jobs))",
            runner,
        )

    def test_parallel_release_and_windows_coverage_paths_are_isolated(self):
        runner = (ROOT / "scripts/run-mir-proof-suite.ps1").read_text(
            encoding="utf-8"
        )
        runall = (ROOT / "scripts/runall.ps1").read_text(encoding="utf-8")

        self.assertIn("function Convert-ToShellPath", runner)
        self.assertIn('cygpath -u -- "$1"', runner)
        self.assertIn(
            "DCC_COVERAGE_BUILD_DIR = Convert-ToShellPath $coverageOutput",
            runner,
        )
        self.assertEqual(
            runall.count('-BuildDir (Join-Path $BuildDir "diagnostics")'),
            2,
        )

    def test_lists_all_gates_without_running_them(self):
        result = subprocess.run(
            [
                PWSH,
                "-NoLogo",
                "-NoProfile",
                "-File",
                "scripts/run-mir-proof-suite.ps1",
                "-List",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=30,
        )

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(
            result.stdout.splitlines(),
            [
                "1. canonical tool build",
                "2. script tests and static audits",
                "3. independent release CMake build",
                "4. normal MIR host tests",
                "5. ASan/UBSan MIR host tests",
                "6. debugger-host tests",
                "7. isolated compiler mutation campaign",
                "8. strict stack release suite",
                "9. strict no-stack release suite",
                "10. extended generated-MIR census",
                "11. instrumented compiler coverage and mutation campaigns",
            ],
        )


if __name__ == "__main__":
    unittest.main()
