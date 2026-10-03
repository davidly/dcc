import importlib.util
import json
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

    def test_cache_controls_cover_individual_joint_and_debug_stack_modes(self):
        with patch.object(self.runner, "compile", return_value=(b"same", "")) as compile:
            self.runner.cache_controls()
        self.assertEqual(compile.call_count, 6 * (len(entrypoints.CACHE_CONTROLS) + 2))
        for debug in ([], ["-g"], ["-gline"]):
            for stack in ([], ["-fstack-check"]):
                calls = [call for call in compile.call_args_list
                         if call.args[1] == debug + stack]
                self.assertEqual(len(calls), len(entrypoints.CACHE_CONTROLS) + 2)
                environments = [call.args[2] for call in calls]
                self.assertTrue(all(env["DCC_MIR_REQUIRE_EMIT"] == "1" and
                                    env["DCC_MIR_REQUIRE_COMPLETE"] == "1"
                                    for env in environments))
                for control in entrypoints.CACHE_CONTROLS:
                    self.assertTrue(any(
                        {key for key in env if key in entrypoints.CACHE_CONTROLS} == {control}
                        for env in environments))
                self.assertTrue(any(
                    all(env.get(control) == "1" for control in entrypoints.CACHE_CONTROLS)
                    for env in environments))
        with patch.object(self.runner, "compile", side_effect=[
                (b"baseline", ""), (b"changed", "")]):
            with self.assertRaisesRegex(AssertionError, "cache/liveness verification changed"):
                self.runner.cache_controls()

    def test_character_contracts_require_every_literal_and_entry_route(self):
        def compile(text, *args, **kwargs):
            self.runner.executions.append({})
            return b"same", ""
        with patch.object(self.runner, "compile", side_effect=compile) as calls:
            self.runner.preprocessor_characters()
        self.assertEqual(calls.call_count, 1 + 3 * len(entrypoints.PP_CHARACTERS))
        self.assertEqual(
            {record["case"] for record in self.runner.executions if "case" in record},
            {"pp-character-" + name + "-" + route
             for name, _, _ in entrypoints.PP_CHARACTERS
             for route in ("if", "elif", "include")})
        for call in calls.call_args_list[1:]:
            self.assertIn("#else\n", call.args[0])
            self.assertIn("#endif\n", call.args[0])
        def drift(text, *args, **kwargs):
            self.runner.executions.append({})
            return (b"same" if text.startswith("int probe") else b"changed"), ""
        with patch.object(self.runner, "compile", side_effect=drift):
            with self.assertRaisesRegex(AssertionError, "character value changed"):
                self.runner.preprocessor_characters()

    def test_proof_manifest_rejects_partial_duplicate_failed_or_changed_evidence(self):
        manifest = json.loads((ROOT / "scripts/compiler-branch-proof.json").read_text())
        records = [{"case": "pp-character-" + name + "-" + route, "status": 0}
                   for name, _, _ in entrypoints.PP_CHARACTERS
                   for route in manifest["character_routes"]]
        records += [{"cases": ["bitset-values-" + str(size)
                               for size in entrypoints.BITSET_SIZES], "status": 0}]
        result = entrypoints.validate_proof_manifest(manifest, records, True)
        self.assertEqual(result, dict(cases=74, complete=True, scope="characters-and-bitsets"))
        for bad in ([], records[:-1], records + [records[0]],
                    [{"case": "unexpected", "status": 0}], [{"cases": "wrong", "status": 0}],
                    [dict(records[0], status=1), *records[1:]]):
            with self.subTest(records=bad), self.assertRaises(AssertionError):
                entrypoints.validate_proof_manifest(manifest, bad, True)
        for field, value in (("version", 2), ("character_cases", []),
                             ("character_routes", ["if"]), ("bitset_sizes", [1])):
            with self.subTest(field=field), self.assertRaises(AssertionError):
                entrypoints.validate_proof_manifest({**manifest, field: value}, records, True)

    def test_analysis_limit_diagnostic_and_partial_output_contracts(self):
        diagnostic = (
            "dcc: fatal: function 'over' is too large to compile: "
            "20485 MIR instructions x 16387 values exceeds the analysis limit "
            "(536870912 cells, 8192 values); split it into smaller functions\n")

        def run(args, **kwargs):
            self.runner.output.write_text("_over:\n")
            return subprocess.CompletedProcess([], 1, "", diagnostic)

        with patch.object(self.runner, "compile", return_value=(b"same", "")), \
                patch.object(self.runner, "run", side_effect=run) as calls:
            self.runner.analysis_limits()
        self.assertEqual(calls.call_count, 2)
        self.assertEqual(calls.call_args_list[1].kwargs["env"], {
            "DCC_MIR_REQUIRE_COMPLETE": "1", "DCC_MIR_REQUIRE_EMIT": "1"})
        self.assertEqual(self.runner.source.read_text().count("*p += 1U;"), 4096)
        for stderr, body in (
                ("generic rejection\n", "_over:\n"),
                (diagnostic.replace("20485", "1").replace("16387", "1"), "_over:\n"),
                (diagnostic, "_over:\nret\n"),
                (diagnostic, "_over:\nend\n")):
            def bad(args, **kwargs):
                self.runner.output.write_text(body)
                return subprocess.CompletedProcess([], 1, "", stderr)
            with self.subTest(stderr=stderr, body=body), \
                    patch.object(self.runner, "compile", return_value=(b"same", "")), \
                    patch.object(self.runner, "run", side_effect=bad):
                with self.assertRaises(AssertionError):
                    self.runner.analysis_limits()

    def test_bitset_proof_requires_all_named_cases_and_completion(self):
        stderr = "".join(f"; MIR bitset-proof values={size} outcome=passed\n"
                         for size in entrypoints.BITSET_SIZES)
        stdout = "MIR bitset layout checks=8 failures=0\n"
        with patch.object(entrypoints.subprocess, "run", return_value=
                          subprocess.CompletedProcess([], 0, stdout, stderr)):
            self.runner.bitset_layout(ROOT / "host")
        for result in (
                subprocess.CompletedProcess([], 1, stdout, stderr),
                subprocess.CompletedProcess([], 0, "", stderr),
                subprocess.CompletedProcess([], 0, stdout, stderr + stderr),
                subprocess.CompletedProcess([], 0, stdout, stderr.replace("values=64", "values=66")),
                subprocess.CompletedProcess([], 0, stdout, stderr.replace("outcome=passed",
                                                                         "outcome=failed"))):
            with self.subTest(result=result), \
                    patch.object(entrypoints.subprocess, "run", return_value=result):
                with self.assertRaises(AssertionError):
                    self.runner.bitset_layout(ROOT / "host")

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
