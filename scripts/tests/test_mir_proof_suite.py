"""Verify the aggregate AST/MIR proof runner's public gate inventory."""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
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
        self.assertIn("[switch]$RequireComplete", runner)
        self.assertIn("[switch]$All", runner)
        self.assertIn(
            'DCC_COVERAGE_REQUIRE_COMPLETE = "1"',
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

    def test_fuzz_generator_and_receipt_are_part_of_the_aggregate_runner(self):
        runner = (ROOT / "scripts/run-mir-proof-suite.ps1").read_text(
            encoding="utf-8"
        )

        self.assertIn("scripts/test-mir-fuzz-source.ps1", runner)
        self.assertIn('Name = "MIR fuzz generator proof"', runner)
        self.assertIn('Join-Path $outputRoot "receipt.json"', runner)
        self.assertIn("ConvertTo-Json -Depth 6", runner)
        self.assertIn("Expected versus found:", runner)
        self.assertIn(
            "export CC=/path/to/clang-18 LLVM_COV=/path/to/llvm-cov-18 LLVM_PROFDATA=/path/to/llvm-profdata-18",
            runner,
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

    def test_all_alias_keeps_the_same_gate_inventory(self):
        result = subprocess.run(
            [
                PWSH,
                "-NoLogo",
                "-NoProfile",
                "-File",
                "scripts/run-mir-proof-suite.ps1",
                "-All",
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

    @unittest.skipIf(os.name == "nt", "POSIX fake tool fixtures")
    def test_llvm_directory_sets_environment_for_children(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            tools = self.make_llvm_tools(directory)
            environment = self.clean_llvm_environment()
            environment["TEST_LLVM_DIRECTORY"] = str(directory)
            environment["TEST_PROOF_RUNNER"] = str(
                ROOT / "scripts/run-mir-proof-suite.ps1"
            )
            command = (
                "& $env:TEST_PROOF_RUNNER -PreflightOnly "
                "-LlvmDirectory $env:TEST_LLVM_DIRECTORY; "
                "if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; "
                "& sh -c 'printf \"child:%s\\n\" \"$CC\" \"$LLVM_COV\" "
                "\"$LLVM_PROFDATA\"'"
            )
            result = subprocess.run(
                [PWSH, "-NoLogo", "-NoProfile", "-Command", command],
                cwd=ROOT, env=environment, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stdout)
            for tool in tools:
                self.assertIn(f"child:{tool}", result.stdout)
            self.assertNotIn("canonical tool build", result.stdout)

    @unittest.skipIf(os.name == "nt", "POSIX fake tool fixtures")
    def test_explicit_llvm_overrides_win_over_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            tools = self.make_llvm_tools(directory)
            environment = self.clean_llvm_environment()
            environment.update(dict(zip(
                ("CC", "LLVM_COV", "LLVM_PROFDATA"), map(str, tools)
            )))
            result = self.llvm_preflight(
                directory / "not-the-explicit-toolchain", environment
            )
            self.assertEqual(result.returncode, 0, result.stdout)
            for tool in tools:
                self.assertIn(str(tool), result.stdout)

    @unittest.skipIf(os.name == "nt", "POSIX fake tool fixtures")
    def test_mismatched_llvm_override_is_not_replaced(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            tools = self.make_llvm_tools(directory)
            wrong = directory / "llvm-cov-19"
            self.write_tool(wrong, "Ubuntu LLVM version 19.0.0")
            environment = self.clean_llvm_environment()
            environment["LLVM_COV"] = str(wrong)
            result = self.llvm_preflight(directory, environment)
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("must all match Clang LLVM 18", result.stdout)
            self.assertIn(str(wrong), result.stdout)
            self.assertIn(str(tools[0]), result.stdout)

    @unittest.skipIf(os.name == "nt", "POSIX fake tool fixtures")
    def test_missing_explicit_peer_is_not_replaced(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.make_llvm_tools(directory)
            environment = self.clean_llvm_environment()
            environment["LLVM_PROFDATA"] = str(directory / "missing-profdata")
            result = self.llvm_preflight(directory, environment)
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("Could not resolve llvm-profdata from LLVM_PROFDATA",
                          result.stdout)

    @unittest.skipIf(os.name == "nt", "POSIX fake tool fixtures")
    def test_repository_local_llvm_works_without_exports(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scripts = root / "scripts"
            scripts.mkdir()
            runner = scripts / "run-mir-proof-suite.ps1"
            shutil.copyfile(ROOT / "scripts/run-mir-proof-suite.ps1", runner)
            directory = root / "build/llvm/bin"
            directory.mkdir(parents=True)
            tools = self.make_llvm_tools(directory)
            environment = self.clean_llvm_environment()
            environment["PATH"] = str(root)
            result = subprocess.run(
                [PWSH, "-NoLogo", "-NoProfile", "-File", str(runner),
                 "-PreflightOnly"],
                cwd=root, env=environment, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stdout)
            for tool in tools:
                self.assertIn(str(tool), result.stdout)

    @unittest.skipIf(os.name == "nt", "POSIX fake tool fixtures")
    def test_xcrun_peers_are_resolved_and_checked(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "LLVM tools"
            directory.mkdir()
            compiler = directory / "clang-18"
            self.write_tool(compiler, "Ubuntu LLVM version 18.0.0")
            peer_directory = root / "peers"
            peer_directory.mkdir()
            cov = peer_directory / "llvm-cov"
            profdata = peer_directory / "llvm-profdata"
            self.write_tool(cov, "Ubuntu LLVM version 18.0.0")
            self.write_tool(profdata, "Ubuntu LLVM version 18.0.0")
            xcrun = directory / "xcrun"
            xcrun.write_text(
                "#!/bin/sh\n"
                f"printf '%s\\n' '{peer_directory}/'$2\n",
                encoding="utf-8",
            )
            xcrun.chmod(0o755)
            environment = self.clean_llvm_environment()
            environment["PATH"] = str(directory)
            result = self.llvm_preflight(directory, environment)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn(str(cov), result.stdout)
            self.assertIn(str(profdata), result.stdout)
            self.write_tool(cov, "Ubuntu LLVM version 19.0.0")
            result = self.llvm_preflight(directory, environment)
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("must all match Clang LLVM 18", result.stdout)

    @staticmethod
    def clean_llvm_environment():
        environment = os.environ.copy()
        for name in ("CC", "LLVM_COV", "LLVM_PROFDATA"):
            environment.pop(name, None)
        return environment

    @staticmethod
    def write_tool(path, version):
        path.write_text(f"#!/bin/sh\nprintf '%s\\n' '{version}'\n",
                        encoding="utf-8")
        path.chmod(0o755)

    def make_llvm_tools(self, directory):
        tools = tuple(directory / name for name in
                      ("clang-18", "llvm-cov-18", "llvm-profdata-18"))
        for tool in tools:
            self.write_tool(tool, "Ubuntu LLVM version 18.0.0")
        return tools

    def llvm_preflight(self, directory, environment):
        return subprocess.run(
            [PWSH, "-NoLogo", "-NoProfile", "-File",
             "scripts/run-mir-proof-suite.ps1", "-PreflightOnly",
             "-LlvmDirectory", str(directory)],
            cwd=ROOT, env=environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30,
        )


if __name__ == "__main__":
    unittest.main()
