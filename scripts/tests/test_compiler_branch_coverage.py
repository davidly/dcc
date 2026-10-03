import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "compiler_branches", ROOT / "scripts/compiler-branch-coverage.py")
COVERAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COVERAGE)


class BranchCoverageTests(unittest.TestCase):
    def setUp(self):
        self.source = "src/dcc/example.c"
        self.inventory = {self.source: [
            dict(range=[2, 1, 8, 2], names=["ordinary"])], "src/dcc/data.c": []}
        self.function = dict(
            name="ordinary", count=1, filenames=[str(ROOT / self.source)],
            regions=[[2, 1, 8, 2, 1, 0, 0, 0]],
            branches=[[3, 5, 3, 10, 1, 0, 0, 0, 4]])
        self.file = dict(
            filename=str(ROOT / self.source), branches=copy.deepcopy(self.function["branches"]),
            expansions=[], summary=dict(functions=dict(count=1, covered=1),
                                        branches=dict(count=2, covered=1, notcovered=1)))
        self.report = dict(type="llvm.coverage.json.export",
                           data=[dict(files=[self.file], functions=[self.function])])
        self.set_counts(2, 1)

    def set_counts(self, total, covered):
        self.file["summary"]["branches"] = dict(
            count=total, covered=covered, notcovered=total - covered)
        row = f"0 0 0.00% 1 0 100.00% 0 0 0.00% {total} {total - covered} 70.00%"
        self.native = f"example.c {row}\nTOTAL {row}\n"

    def summarize(self):
        return COVERAGE.summarize(self.report, self.inventory, self.native, ROOT)

    def macros(self, nested=False):
        function = self.function
        function["filenames"] += [str(ROOT / "src/dcc/example.h")] * (3 if nested else 2)
        regions = [[4, 8, 4, 12, 1, 0, 1, 1], [5, 8, 5, 12, 1, 0, 2, 1]]
        if nested:
            regions.append([10, 3, 10, 9, 1, 1, 3, 1])
        function["regions"] += regions
        branches = [[10, 2, 10, 6, 1, 1, 1, 0, 4],
                    [10, 2, 10, 6, 0, 1, 2, 0, 4]]
        if nested:
            branches.append([12, 2, 12, 6, 1, 0, 3, 0, 4])
        function["branches"] += branches
        for index, region in enumerate(regions[:2]):
            included = [branches[index]]
            if nested and index == 0:
                included.append(branches[2])
            self.file["expansions"].append(dict(
                source_region=copy.deepcopy(region), filenames=function["filenames"],
                target_regions=copy.deepcopy(function["regions"]),
                branches=copy.deepcopy(included)))
        self.set_counts(8 if nested else 6, 5 if nested else 4)

    def test_exact_integer_boundaries(self):
        for total, covered, passes in (
                (170672, 119470, False), (170672, 119471, True),
                (170874, 119611, False), (170874, 119612, True),
                (10, 6, False), (10, 7, True), (3, 2, False), (3, 3, True),
                (0, 0, False), (1, 0, False), (1, 1, True)):
            with self.subTest(total=total, covered=covered):
                if passes:
                    COVERAGE.require_target(dict(totals=dict(count=total, covered=covered)))
                else:
                    with self.assertRaises(ValueError):
                        COVERAGE.require_target(dict(totals=dict(count=total, covered=covered)))
        for total, covered in ((True, 1), (10, -1), (10, 11), (10, 7.0)):
            with self.assertRaises(ValueError):
                COVERAGE.require_target(dict(totals=dict(count=total, covered=covered)))

    def test_scope_excludes_host_and_includes_data_only_file(self):
        host = copy.deepcopy(self.file)
        host["filename"] = str(ROOT / "tests/host/example.c")
        self.report["data"][0]["files"].append(host)
        summary = self.summarize()
        self.assertEqual(summary["totals"], dict(count=2, covered=1, missed=1))
        self.assertEqual(summary["required"], 2)
        self.assertEqual(summary["deficit"], 1)
        self.assertFalse(summary["passes"])
        self.assertEqual(summary["files"]["src/dcc/data.c"]["count"], 0)

    def test_aliases_are_one_source_body_not_summed(self):
        self.inventory[self.source][0]["names"].append("alias")
        alias = copy.deepcopy(self.function)
        alias["name"] = "alias"
        alias["count"] = 0
        alias["branches"][0][4] = 0
        self.report["data"][0]["functions"].append(alias)
        self.file["branches"] += copy.deepcopy(alias["branches"])
        summary = self.summarize()
        self.assertEqual(summary["totals"]["count"], 2)
        self.assertEqual(summary["totals"]["covered"], 1)
        self.assertEqual(len(summary["aliases"]), 1)
        self.set_counts(4, 1)
        with self.assertRaisesRegex(ValueError, "LLVM file summary"):
            self.summarize()

    def test_alias_union_cannot_inflate_native_maximum(self):
        self.inventory[self.source][0]["names"].append("alias")
        alias = copy.deepcopy(self.function)
        alias["name"] = "alias"
        alias["branches"][0][4:6] = [0, 1]
        self.report["data"][0]["functions"].append(alias)
        self.file["branches"] += copy.deepcopy(alias["branches"])
        with self.assertRaisesRegex(ValueError, "native maximum"):
            self.summarize()

    def test_repeated_and_nested_macros_have_distinct_invocation_ids(self):
        for nested in (False, True):
            self.setUp()
            self.macros(nested)
            summary = self.summarize()
            self.assertEqual(summary["totals"]["count"], 8 if nested else 6)
            ids = [item["id"] for item in summary["outcomes"]]
            self.assertEqual(len(ids), len(set(ids)))
            paths = [item["expansions"] for item in summary["outcomes"]]
            if nested:
                self.assertTrue(any(len(path) == 2 for path in paths))

    def test_missing_macro_records_and_count_disagreement_fail(self):
        self.macros()
        original = copy.deepcopy(self.report)
        mutations = [
            lambda: self.file.update(expansions=[]),
            lambda: self.file["expansions"][0].update(branches=[]),
            lambda: self.file["expansions"][0]["branches"][0].__setitem__(4, 99),
            lambda: self.function["branches"][1].__setitem__(6, 99),
            lambda: self.function["regions"].append(self.function["regions"][1]),
            lambda: self.function["regions"][1].__setitem__(5, 1),
        ]
        for mutate in mutations:
            self.report = copy.deepcopy(original)
            self.file = self.report["data"][0]["files"][0]
            self.function = self.report["data"][0]["functions"][0]
            mutate()
            with self.assertRaises(ValueError):
                self.summarize()

    def test_file_only_folded_regions_are_explicit_and_never_credited(self):
        self.file["branches"].append([6, 5, 6, 9, 0, 0, 0, 0, 4])
        summary = self.summarize()
        self.assertEqual(summary["totals"]["count"], 2)
        self.assertEqual(summary["folded_file_regions"][self.source], [[6, 5, 6, 9]])
        self.file["branches"][-1][4] = 1
        with self.assertRaisesRegex(ValueError, "unexpected executed"):
            self.summarize()

    def test_empty_partial_wrong_and_inconsistent_exports_fail(self):
        original = copy.deepcopy(self.report)
        mutations = [
            lambda: self.report.update(data=[]),
            lambda: self.report.update(type="other"),
            lambda: self.report["data"][0].update(files=[]),
            lambda: self.report["data"][0].update(functions=[]),
            lambda: self.file.update(branches=[]),
            lambda: self.file.pop("expansions"),
            lambda: self.function["branches"].append(self.function["branches"][0]),
            lambda: self.function["branches"][0].__setitem__(4, True),
            lambda: self.function["branches"][0].__setitem__(5, -1),
            lambda: self.function["branches"][0].__setitem__(0, 0),
            lambda: self.function["branches"][0].__setitem__(8, 99),
            lambda: self.file["summary"]["branches"].update(count=4),
            lambda: self.file["summary"]["branches"].update(notcovered=99),
            lambda: self.file.update(filename=str(ROOT / "tests/host/example.c")),
            lambda: self.report["data"][0]["functions"].append(copy.deepcopy(self.function)),
        ]
        for mutate in mutations:
            self.report = copy.deepcopy(original)
            self.file = self.report["data"][0]["files"][0]
            self.function = self.report["data"][0]["functions"][0]
            mutate()
            with self.assertRaises((ValueError, KeyError)):
                self.summarize()

    def test_native_disagreement_missing_or_duplicate_fails(self):
        native = self.native
        for value in ("", native.splitlines()[0], native + native,
                      native.replace("2 1 70.00%", "2 0 70.00%")):
            self.native = value
            with self.assertRaises(ValueError):
                self.summarize()

    def test_comparison_preserves_outcome_identity_and_reports_losses(self):
        before = self.summarize()
        self.function["branches"][0][4:6] = [0, 2]
        self.file["branches"][0][4:6] = [0, 2]
        after = self.summarize()
        delta = COVERAGE.compare(after, before)
        self.assertEqual(len(delta["new"]), 1)
        self.assertEqual(len(delta["lost"]), 1)
        before["outcomes"].pop()
        with self.assertRaisesRegex(ValueError, "mapping changed"):
            COVERAGE.compare(after, before)
        after["outcomes"].append(after["outcomes"][0])
        with self.assertRaisesRegex(ValueError, "duplicate comparison"):
            COVERAGE.compare(after, after)

    def test_cli_missing_inputs_fail_nonzero(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            process = subprocess.run(
                ["python3", str(ROOT / "scripts/compiler-branch-coverage.py"),
                 "--coverage", str(path / "missing"), "--inventory", str(path / "missing"),
                 "--native-report", str(path / "missing"), "--output-dir", str(path / "out")],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(process.returncode, 1)
            self.assertIn("compiler-branch-coverage:", process.stderr)
            self.assertFalse((path / "out/compiler-branch-coverage.json").exists())


@unittest.skipUnless(all(shutil.which(name) for name in
                        ("clang", "llvm-cov", "llvm-profdata")), "LLVM tools unavailable")
class NativeBranchMappingTests(unittest.TestCase):
    def test_real_aliases_repeated_header_and_nested_macros(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            source = root / "src/dcc"
            source.mkdir(parents=True)
            (source / "condition.h").write_text(
                "#define INNER(x) ((x) > 0)\n#define OUTER(x) INNER(x)\n")
            driver = source / "dcc.c"
            driver.write_text(
                '#include "condition.h"\n'
                "int check(int x) {\n"
                " if (OUTER(x)) return 1;\n"
                " if (OUTER(x)) return 2;\n"
                " return 0;\n}\n"
                "int main(void) { return check(1) != 1; }\n")
            host = root / "host.c"
            host.write_text(
                "#define main dcc_driver_main\n#define check host_check\n"
                '#include "src/dcc/dcc.c"\n')
            binary = root / "compiler"
            flags = ["-fprofile-instr-generate", "-fcoverage-mapping"]
            subprocess.run(["clang", *flags, str(driver), str(host), "-o", str(binary)],
                           check=True, capture_output=True, text=True, timeout=30)
            raw = root / "run.profraw"
            subprocess.run([str(binary)], check=True,
                           env=dict(os.environ, LLVM_PROFILE_FILE=str(raw)), timeout=30)
            profile = root / "run.profdata"
            subprocess.run(["llvm-profdata", "merge", str(raw), "-o", str(profile)],
                           check=True, capture_output=True, text=True, timeout=30)
            command = [str(binary), f"-instr-profile={profile}"]
            report = json.loads(subprocess.check_output(
                ["llvm-cov", "export", *command], text=True, timeout=30))
            native = subprocess.check_output(
                ["llvm-cov", "report", *command, str(driver)], text=True, timeout=30)
            bodies = {}
            for path in (driver, host):
                tree = json.loads(subprocess.check_output(
                    ["clang", "-fsyntax-only", "-Xclang", "-ast-dump=json", str(path)],
                    text=True, timeout=30))
                for key, names in COVERAGE.FUNCTIONS.ast_definitions(tree, root).items():
                    bodies.setdefault(key, set()).update(names)
            inventory = {"src/dcc/dcc.c": [
                dict(range=list(key[1:]), names=sorted(names))
                for key, names in sorted(bodies.items())]}
            result = COVERAGE.summarize(report, inventory, native, root)
            self.assertGreater(result["totals"]["count"], 0)
            self.assertEqual(len(result["aliases"]), 2)
            expanded = [item for item in result["outcomes"] if item["expansions"]]
            self.assertTrue(expanded)
            self.assertTrue(any(len(item["expansions"]) == 2 for item in expanded))
            sites = {tuple(item["expansions"][0]) for item in expanded}
            self.assertEqual(len(sites), 2)
            self.assertEqual(len(result["outcomes"]),
                             len({item["id"] for item in result["outcomes"]}))
            export = root / "export.json"
            export.write_text(json.dumps(report))
            definitions = root / "inventory.json"
            definitions.write_text(json.dumps(inventory))
            native_report = root / "summary.txt"
            native_report.write_text(native)
            output = root / "report"
            command = ["python3", str(ROOT / "scripts/compiler-branch-coverage.py"),
                       "--repo", str(root), "--coverage", str(export),
                       "--inventory", str(definitions), "--native-report", str(native_report),
                       "--output-dir", str(output)]
            diagnostic = subprocess.run(
                [*command, "--allow-below-target"], capture_output=True, text=True, timeout=30)
            self.assertEqual(diagnostic.returncode, 0, diagnostic.stderr)
            gated = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(gated.returncode, 0 if result["passes"] else 1, gated.stderr)
            written = json.loads((output / "compiler-branch-coverage.json").read_text())
            self.assertEqual(written["totals"], result["totals"])
            (source / "uncompiled.c").write_text("int omitted(void) { return 0; }\n")
            incomplete = subprocess.run(
                [*command, "--allow-below-target"], capture_output=True, text=True, timeout=30)
            self.assertEqual(incomplete.returncode, 1)
            self.assertIn("maintained src/dcc", incomplete.stderr)


if __name__ == "__main__":
    unittest.main()
