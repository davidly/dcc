import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "compiler_functions", ROOT / "scripts/compiler-function-coverage.py")
coverage = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(coverage)


class SourceFunctionTests(unittest.TestCase):
    def setUp(self):
        self.root = ROOT
        self.source = "src/dcc/example.c"
        self.inventory = {self.source: [
            {"range": [2, 1, 4, 2], "names": ["main"]}],
            "src/dcc/data.c": []}
        self.report = {
            "type": "llvm.coverage.json.export",
            "data": [{"files": [{
                "filename": str(ROOT / self.source),
                "summary": {"functions": {"count": 1, "covered": 1, "percent": 100}}},
            ], "functions": [{
                "name": "main", "count": 1, "filenames": [str(ROOT / self.source)],
                "regions": [[2, 1, 4, 2, 1, 0, 0, 0]]}]}]}
        self.native = (
            "example.c 10 2 80.00% 1 0 100.00% 10 2 80.00% 10 2 80.00%\n"
            "TOTAL 10 2 80.00% 1 0 100.00% 10 2 80.00% 10 2 80.00%\n")

    def summarize(self):
        return coverage.summarize(self.report, self.inventory, self.native, self.root)

    def test_source_function_alias_not_extra_denominator(self):
        self.inventory[self.source][0]["names"].append("dcc_driver_main")
        alias = copy.deepcopy(self.report["data"][0]["functions"][0])
        alias.update(name="dcc_driver_main", count=0)
        self.report["data"][0]["functions"].append(alias)
        summary = self.summarize()
        coverage.require_complete(summary)
        self.assertEqual(summary["totals"], {"count": 1, "covered": 1, "missed": 0})
        self.assertEqual(len(summary["aliases"]), 1)
        self.assertEqual(summary["aliases"][0]["identities"][1]["count"], 0)
        self.assertEqual(summary["gaps"], [])

    def test_unknown_extra_alias_rejected(self):
        alias = copy.deepcopy(self.report["data"][0]["functions"][0])
        alias.update(name="unknown_extra_alias", count=0)
        self.report["data"][0]["functions"].append(alias)
        with self.assertRaisesRegex(ValueError, "unexpected function identities.*unknown_extra_alias"):
            self.summarize()

    def test_missing_independently_compiled_alias_is_partial_export(self):
        self.inventory[self.source][0]["names"].append("dcc_driver_main")
        with self.assertRaisesRegex(ValueError, "missing function identities"):
            self.summarize()

    def test_zero_function_file_explicit_and_absent(self):
        self.assertEqual(self.summarize()["files"]["src/dcc/data.c"]["count"], 0)
        self.report["data"][0]["files"].append({
            "filename": str(ROOT / "src/dcc/data.c"),
            "summary": {"functions": {"count": 0, "covered": 0}}})
        self.native = "data.c 0 0 0.00% 0 0 0.00% 0 0 0.00% 0 0 0.00%\n" + self.native
        coverage.require_complete(self.summarize())

    def test_rounded_percentage_cannot_hide_gap(self):
        self.report["data"][0]["functions"][0]["count"] = 0
        self.report["data"][0]["files"][0]["summary"]["functions"]["covered"] = 0
        self.native = self.native.replace("1 0 100.00%", "1 1 100.00%")
        summary = self.summarize()
        self.assertEqual(len(summary["gaps"]), 1)
        with self.assertRaisesRegex(ValueError, "missed=1"):
            coverage.require_complete(summary)
        with self.assertRaises(ValueError):
            coverage.require_complete({"totals": {"count": 100000, "covered": 99999, "missed": 1}})

    def test_missing_partial_empty_invalid_export(self):
        mutations = [
            lambda r: r.update(data=[]),
            lambda r: r.update(type="other"),
            lambda r: r["data"][0].update(files=[]),
            lambda r: r["data"][0].update(functions=[]),
            lambda r: r["data"][0]["files"][0]["summary"]["functions"].update(count=2),
            lambda r: r["data"][0]["files"][0]["summary"]["functions"].update(covered=2),
            lambda r: r["data"][0]["files"][0]["summary"]["functions"].update(count=-1),
            lambda r: r["data"][0]["files"][0]["summary"]["functions"].update(count=True),
            lambda r: r["data"][0]["functions"][0].update(count=-1),
            lambda r: r["data"][0]["functions"][0].update(regions=[]),
            lambda r: r["data"][0]["files"].append(copy.deepcopy(r["data"][0]["files"][0])),
            lambda r: r["data"].append(copy.deepcopy(r["data"][0])),
        ]
        original = copy.deepcopy(self.report)
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                self.report = copy.deepcopy(original)
                mutation(self.report)
                with self.assertRaises(ValueError):
                    self.summarize()

    def test_missing_partial_empty_invalid_inventory(self):
        for inventory in ({}, {"src/dcc/data.c": []},
                          {self.source: []}, {self.source: self.inventory[self.source] * 2}):
            with self.subTest(inventory=inventory), self.assertRaises(ValueError):
                coverage.summarize(self.report, inventory, self.native, self.root)

    def test_missing_partial_invalid_native(self):
        for native in ("", self.native.splitlines()[0], self.native.splitlines()[1],
                       self.native.replace("1 0 100", "1 -1 100"),
                       self.native.replace("TOTAL 10 2 80.00% 1", "TOTAL 10 2 80.00% 2")):
            with self.subTest(native=native), self.assertRaises(ValueError):
                coverage.summarize(self.report, self.inventory, native, self.root)

    def test_extra_function_and_source_rejected(self):
        self.report["data"][0]["functions"][0]["regions"][0][0] = 9
        with self.assertRaisesRegex(ValueError, "outside independent inventory"):
            self.summarize()


class CompilationInventoryTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build/compiler-function-coverage").mkdir(parents=True, exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(dir=ROOT / "build/compiler-function-coverage")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()
        source = self.root / "src/dcc"
        source.mkdir(parents=True)
        (source / "dcc.c").write_text("int main(void) { return 0; }\n")
        (source / "data.c").write_text("int global;\n")
        (source / "conditional.c").write_text(
            "int ordinary(void) { return 1; }\n"
            "#ifdef HOST_ONLY\nint host_only(void) { return 2; }\n#endif\n"
            "#ifdef DISABLED\nint disabled(void) { return 3; }\n#endif\n")
        host = self.root / "tests/host"
        host.mkdir(parents=True)
        self.entries = []
        for target in coverage.TARGETS:
            sources = sorted(source.glob("*.c"))
            if target != "dcc":
                sources.remove(source / "dcc.c")
                included = host / (target + ".c")
                included.write_text('#define main dcc_driver_main\n'
                                    '#include "../../src/dcc/dcc.c"\n'
                                    '#undef main\nint main(void) { return 0; }\n')
                sources.append(included)
            for path in sources:
                args = ["clang", "-std=c11"]
                if target == "mir-vla-smooth-isolation-test":
                    args.append("-DHOST_ONLY=1")
                args += ["-o", f"CMakeFiles/{target}.dir/{path.name}.o", "-c", str(path)]
                self.entries.append(dict(directory=str(self.root), file=str(path), arguments=args))
        self.database = self.root / "compile_commands.json"

    def inventory(self):
        self.database.write_text(json.dumps(self.entries))
        return coverage.compiled_inventory(self.database, self.root, jobs=2)

    def test_compilation_defines_and_data_only_inventory(self):
        found = self.inventory()
        self.assertEqual(set(found), {"src/dcc/dcc.c", "src/dcc/data.c", "src/dcc/conditional.c"})
        self.assertEqual(found["src/dcc/data.c"], [])
        self.assertEqual(found["src/dcc/dcc.c"][0]["names"], ["dcc_driver_main", "main"])
        names = {name for function in found["src/dcc/conditional.c"] for name in function["names"]}
        self.assertEqual(names, {"ordinary", "host_only"})

    def test_partial_target_and_new_uncompiled_source_fail_closed(self):
        self.entries.pop()
        with self.assertRaisesRegex(ValueError, "missing host translation"):
            self.inventory()
        (self.root / "src/dcc/new.c").write_text("int new_function(void) {return 0;}\n")
        with self.assertRaisesRegex(ValueError, "incomplete compiled inventory"):
            self.inventory()

    def test_empty_inventory_and_ast_subprocess_failure(self):
        entries = self.entries
        self.entries = []
        with self.assertRaisesRegex(ValueError, "empty compilation"):
            self.inventory()
        self.entries = entries
        with patch.object(coverage.subprocess, "run", side_effect=subprocess.TimeoutExpired("clang", 120)):
            with self.assertRaises(subprocess.TimeoutExpired):
                self.inventory()

    def test_cli_missing_and_invalid_data_fail(self):
        script = ROOT / "scripts/compiler-function-coverage.py"
        self.database.write_text("invalid")
        process = subprocess.run(
            ["python3", str(script), "--compile-commands", str(self.database),
             "--coverage", str(self.root / "missing.json"),
             "--native-report", str(self.root / "missing.txt"),
             "--output-dir", str(self.root / "report")],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(process.returncode, 1)
        self.assertIn("compiler-function-coverage:", process.stderr)

    def test_incomplete_cli_exports_gaps_and_propagates_failure(self):
        inventory = self.inventory()
        files, functions, native = [], [], []
        total, covered = 0, 0
        for source, definitions in inventory.items():
            hit = len(definitions) - (source == "src/dcc/dcc.c")
            files.append(dict(filename=str(self.root / source),
                              summary=dict(functions=dict(count=len(definitions), covered=hit))))
            for definition in definitions:
                count = 0 if source == "src/dcc/dcc.c" else 1
                for name in definition["names"]:
                    functions.append(dict(name=name, count=count,
                                          filenames=[str(self.root / source)],
                                          regions=[[*definition["range"], count, 0, 0, 0]]))
            native.append(f"{Path(source).name} 0 0 0.00% {len(definitions)} "
                          f"{len(definitions) - hit} 100.00% 0 0 0.00% 0 0 0.00%")
            total += len(definitions)
            covered += hit
        native.append(f"TOTAL 0 0 0.00% {total} {total - covered} 100.00% "
                      "0 0 0.00% 0 0 0.00%")
        export = self.root / "coverage.json"
        export.write_text(json.dumps(dict(type="llvm.coverage.json.export",
                                         data=[dict(files=files, functions=functions)])))
        report = self.root / "native.txt"
        report.write_text("\n".join(native))
        output = self.root / "report"
        result = subprocess.run(
            ["python3", str(ROOT / "scripts/compiler-function-coverage.py"),
             "--repo", str(self.root), "--compile-commands", str(self.database),
             "--coverage", str(export), "--native-report", str(report),
             "--output-dir", str(output)],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("missed=1", result.stderr)
        summary = json.loads((output / "compiler-function-coverage.json").read_text())
        self.assertEqual(summary["totals"]["missed"], 1)
        self.assertEqual(len(json.loads((output / "compiler-function-gaps.json").read_text())), 1)


if __name__ == "__main__":
    unittest.main()
