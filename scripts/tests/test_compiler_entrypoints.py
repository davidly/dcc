import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "compiler_entrypoints", ROOT / "scripts/test-compiler-entrypoints.py")
entrypoints = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(entrypoints)


class EntrypointTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build/compiler-function-coverage").mkdir(parents=True, exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(dir=ROOT / "build/compiler-function-coverage")
        self.addCleanup(self.directory.cleanup)
        self.runner = entrypoints.Runner(ROOT / "dcc", Path(self.directory.name), 7)

    def test_private_environment_preserves_profile(self):
        with patch.dict(os.environ, {"LLVM_PROFILE_FILE": "private-%8m.profraw",
                                     "DCC_MIR_SCHEDULE_REQUIRE": "1",
                                     "DCC_MIR_TARGET_FUNCTION": "wrong",
                                     "DCC_MIR_MACHINE_MUTATE": "bad", "DCC_AST_DUMP": "1"}):
            env = entrypoints.private_environment()
            self.assertEqual(env["LLVM_PROFILE_FILE"], "private-%8m.profraw")
            self.assertFalse(any(key.startswith("DCC_MIR_") for key in env))
            self.assertNotIn("DCC_AST_DUMP", env)

    def test_status_stdout_stderr_timeout_and_unique_paths(self):
        completed = subprocess.CompletedProcess([], 1, "", entrypoints.USAGE)
        with patch.object(entrypoints.subprocess, "run", return_value=completed) as run:
            self.runner.run([], status=1, stderr=entrypoints.USAGE)
            args = run.call_args.kwargs
            self.assertEqual(args["timeout"], 7)
            self.assertEqual(args["cwd"], self.runner.directory)
            self.assertIsNot(args["env"], os.environ)
        other = entrypoints.Runner(ROOT / "dcc", Path(self.directory.name), 7)
        self.assertNotEqual(other.directory, self.runner.directory)
        for result in (subprocess.CompletedProcess([], 0, "", entrypoints.USAGE),
                       subprocess.CompletedProcess([], 1, "unexpected", entrypoints.USAGE),
                       subprocess.CompletedProcess([], 1, "", "wrong diagnostic")):
            with patch.object(entrypoints.subprocess, "run", return_value=result):
                with self.assertRaises(AssertionError):
                    self.runner.run([], status=1, stderr=entrypoints.USAGE)
        with patch.object(entrypoints.subprocess, "run",
                          side_effect=subprocess.TimeoutExpired("dcc", 7)):
            with self.assertRaises(subprocess.TimeoutExpired):
                self.runner.run([])

    def test_report_schema_rejects_partial_invalid_duplicate(self):
        fields = " ".join(f"{key}={'probe' if key == 'function' else 0}"
                          for key in sorted(entrypoints.SCHEDULE_FIELDS))
        line = "; MIR schedule-plan " + fields + "\n"
        self.assertEqual(entrypoints.parse_reports(line, target=False)["schedule"]["valid"], 0)
        for bad in ("", line + line, line.replace("valid=0", "valid=-1"),
                    line.replace("valid=0", ""), line + "unexpected\n"):
            with self.subTest(bad=bad), self.assertRaises(AssertionError):
                entrypoints.parse_reports(bad, target=False)

    def test_isolated_invalid_shadow_report_and_require(self):
        fields = {key: 0 for key in entrypoints.SCHEDULE_FIELDS}
        fields.update(function="coverage_unsupported", unsupported=1)
        line = "; MIR schedule-plan " + " ".join(f"{key}={value}"
                                                 for key, value in fields.items()) + "\n"
        report = subprocess.CompletedProcess([], 0, "", line)
        required = subprocess.CompletedProcess(
            [], 1, "", line + "dcc: fatal: cannot build MIR shadow schedule\n")
        with patch.object(entrypoints.subprocess, "run", side_effect=[report, required]) as run:
            self.runner.invalid_shadow(ROOT / "host")
            report_env = run.call_args_list[0].kwargs["env"]
            require_env = run.call_args_list[1].kwargs["env"]
            self.assertNotIn("DCC_MIR_SCHEDULE_REQUIRE", report_env)
            self.assertEqual(require_env["DCC_MIR_SCHEDULE_REQUIRE"], "1")
            self.assertEqual(require_env["DCC_MIR_SCHEDULE_FUNCTION"], "coverage_unsupported")

    def test_collection_runner_failure_prevents_stamp(self):
        shell = (ROOT / "scripts/compiler-coverage.sh").read_text()
        runner = shell.index('python3 "$repo_root/scripts/test-compiler-entrypoints.py"')
        stamp = shell.index('python3 "$checkpoint" collected')
        self.assertLess(runner, stamp)
        self.assertIn("set -eu", shell)
        self.assertIn('--host "$build_dir/cmake/mir-verify-test"', shell)
        self.assertIn("export LLVM_PROFILE_FILE=", shell[:runner])

    def test_report_only_provenance_before_export_and_exact_gate(self):
        shell = (ROOT / "scripts/compiler-coverage.sh").read_text()
        check = shell.index('python3 "$checkpoint" report')
        export = shell.index('>"$report_dir/compiler-coverage.json"')
        gate = shell.index('python3 "$repo_root/scripts/compiler-function-coverage.py"')
        self.assertLess(check, export)
        self.assertGreater(gate, shell.index('-output-dir="$report_dir/html"'))
        self.assertNotIn("DCC_COVERAGE_REQUIRE_COMPLETE", shell[gate:])
        self.assertEqual(shell.count('-object "$build_dir/cmake/mir-selector-isolation-test"'), 7)

    def shell_checkpoint(self):
        spec = importlib.util.spec_from_file_location(
            "entrypoint_checkpoint", ROOT / "scripts/coverage-checkpoint.py")
        checkpoint = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(checkpoint)
        root = Path(self.directory.name) / "repository"
        scripts = root / "scripts"
        scripts.mkdir(parents=True)
        for name in ("compiler-coverage.sh", "coverage-checkpoint.py"):
            shutil.copyfile(ROOT / "scripts" / name, scripts / name)
        (scripts / "coverage-sources.sh").write_text('printf "%s\\n" "$PWD/src/dcc/dcc.c"\n')
        (scripts / "ast-function-coverage.py").write_text("pass\n")
        (scripts / "test-compiler-entrypoints.py").write_text(
            "import sys\nprint('deliberate entrypoint failure', file=sys.stderr)\nsys.exit(7)\n")
        source = root / "src/dcc/dcc.c"
        source.parent.mkdir(parents=True)
        source.write_text("int main(void) { return 0; }\n")
        subprocess.run(["git", "init", "-q", str(root)], check=True)
        subprocess.run(["git", "-C", str(root), "-c", "user.name=Test",
                        "-c", "user.email=test@example.invalid", "commit",
                        "--allow-empty", "-qm", "test"], check=True)
        build = root / "build/coverage"
        build.mkdir(parents=True)
        tool = build / "tool"
        tool.write_text("#!/bin/sh\nexit 0\n")
        tool.chmod(0o755)
        checkpoint.prepare(root, build, {"clang": tool})
        checkpoint.finish_build(root, build, {"dcc": tool})
        environment = {**os.environ, "DCC_COVERAGE_BUILD_DIR": str(build),
                       "CC": str(tool), "PWSH": str(tool), "LLVM_COV": str(tool),
                       "LLVM_PROFDATA": str(tool)}
        return root, build, environment

    def test_actual_collection_failure_removes_success_stamp(self):
        root, build, environment = self.shell_checkpoint()
        (build / "collection.json").write_text('{"stale": true}\n')
        environment["DCC_COVERAGE_STAGE"] = "collect"
        result = subprocess.run(["sh", str(root / "scripts/compiler-coverage.sh")],
                                env=environment, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 7, result.stderr)
        self.assertIn("deliberate entrypoint failure", result.stderr)
        self.assertFalse((build / "collection.json").exists())
        self.assertFalse((build / ".coverage-lock").exists())

    def test_actual_report_only_requires_completed_provenance(self):
        root, build, environment = self.shell_checkpoint()
        environment["DCC_COVERAGE_STAGE"] = "report"
        result = subprocess.run(["sh", str(root / "scripts/compiler-coverage.sh")],
                                env=environment, capture_output=True, text=True, timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing coverage checkpoint", result.stderr)
        self.assertFalse((build / "dcc.profdata").exists())
        self.assertFalse((build / "report/compiler-coverage.json").exists())


if __name__ == "__main__":
    unittest.main()
