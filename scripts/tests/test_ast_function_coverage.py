import importlib.util
from pathlib import Path
import unittest


SPEC = importlib.util.spec_from_file_location(
    "ast_function_coverage", Path(__file__).resolve().parents[1] / "ast-function-coverage.py")
coverage = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(coverage)


class FunctionCoverageTests(unittest.TestCase):
    def test_definitions_exclude_prototypes_and_headers(self):
        tree = {"inner": [
            {"kind": "FunctionDecl", "name": "prototype"},
            {"kind": "FunctionDecl", "name": "header", "loc": {"includedFrom": {}},
             "inner": [{"kind": "CompoundStmt"}]},
            {"kind": "FunctionDecl", "name": "active", "loc": {"line": 10},
             "inner": [{"kind": "CompoundStmt", "inner": [
                 {"kind": "DeclRefExpr", "referencedDecl": {
                     "kind": "FunctionDecl", "name": "callee"}}]}]},
        ]}
        self.assertEqual(coverage.definitions(tree, Path("test.c")), {
            "active": {"line": 10, "calls": ["callee"]}})

    def test_unclassified_definition_fails(self):
        with self.assertRaisesRegex(ValueError, "unclassified"):
            coverage.validate({"source": {"new_function": {}}}, {})

    def test_removed_definition_fails(self):
        with self.assertRaisesRegex(ValueError, "stale"):
            coverage.validate({}, {("source", "old_function"): {}})

    def test_complete_inventory_passes(self):
        coverage.validate({"source": {"function": {}}}, {("source", "function"): {}})

    def test_new_legacy_reference_fails(self):
        found = {"source": {"active": {"calls": ["old"]}, "old": {"calls": []}}}
        classified = {("source", "active"): "production", ("source", "old"): "legacy"}
        with self.assertRaisesRegex(ValueError, "changed legacy boundaries"):
            coverage.validate_edges(found, classified, [])
        coverage.validate_edges(found, classified, [["active", "old", "MIR guard"]])

    def test_zero_count_active_function_is_included(self):
        report = {"data": [{"functions": [{"name": "source.c:active", "count": 0,
                   "filenames": [str(coverage.ROOT / "source.c")]}]}]}
        self.assertEqual(coverage.select_functions(report, {("source.c", "active"): "production"}, set()),
                         ["source.c:active"])

    def test_executed_legacy_exclusion_fails(self):
        report = {"data": [{"functions": [{"name": "old", "count": 1,
                   "filenames": [str(coverage.ROOT / "source.c")]}]}]}
        with self.assertRaisesRegex(ValueError, "executed legacy"):
            coverage.select_functions(report, {("source.c", "old"): "legacy"}, set())

    def test_missing_coverage_function_fails(self):
        with self.assertRaisesRegex(ValueError, "missing classified"):
            coverage.select_functions({"data": [{"functions": []}]},
                                      {("source.c", "active"): "production"}, set())

    def test_native_metrics_and_exact_name_check(self):
        text = "active 10 2 80.00% 8 1 87.50% 4 2 50.00%\nTOTAL 10 2 80.00% 8 1 87.50% 4 2 50.00%\n"
        result = coverage.summarize_native(text, {"active"})
        self.assertEqual(result["totals"]["lines"], {"count": 8, "covered": 7})
        excluded = "old 10 10 0.00% 8 8 0.00% 4 4 0.00%\n"
        result = coverage.summarize_native(text + excluded, {"active"}, {"old"})
        self.assertEqual(result["totals"]["lines"], {"count": 8, "covered": 7})
        with self.assertRaisesRegex(ValueError, "exact function selection"):
            coverage.summarize_native(text, {"active", "missing"})
        with self.assertRaisesRegex(ValueError, "unexpected or duplicate"):
            coverage.summarize_native(text, {"other"})
        with self.assertRaisesRegex(ValueError, "unexpected or duplicate"):
            coverage.summarize_native(text + text, {"active"})
        with self.assertRaisesRegex(ValueError, "overlap"):
            coverage.summarize_native(text, {"active"}, {"active"})

    def test_excluded_function_names_follow_coverage_identity(self):
        report = {"data": [{"functions": [
            {"name": "source.c:old", "count": 0,
             "filenames": [str(coverage.ROOT / "source.c")]},
            {"name": "source.c:active", "count": 1,
             "filenames": [str(coverage.ROOT / "source.c")]},
        ]}]}
        classified = {
            ("source.c", "old"): "legacy",
            ("source.c", "active"): "production",
        }
        self.assertEqual(
            coverage.excluded_functions(report, classified), {"source.c:old"})


    def test_gap_ledger_keeps_uncovered_outcomes_and_scope(self):
        source = str(coverage.ROOT / "src/dcc/dcc_mir_verify.c")
        active = {"name": "active", "count": 1, "filenames": [source],
                  "branches": [[10, 2, 10, 9, 0, 3, 0, 0, 4]]}
        old = dict(active, name="legacy", count=0)
        report = {"data": [{"functions": [active, old]}]}
        result = coverage.coverage_gaps(report, {"active"})
        self.assertEqual(result["unexecuted_functions"], [])
        self.assertEqual(len(result["uncovered_branch_outcomes"]), 1)
        self.assertEqual(result["uncovered_branch_outcomes"][0]["outcome"], "true")
        self.assertEqual(result["uncovered_branch_outcomes"][0]["review"], "unreviewed")
        report["data"].append({"functions": [dict(active, branches=[[10, 2, 10, 9, 1, 0, 0, 0, 4]])]})
        self.assertEqual(coverage.coverage_gaps(report, {"active"})["uncovered_branch_outcomes"], [])

    def test_reviews_preserve_denominator_and_require_evidence(self):
        review = {"source": "src/dcc/dcc_mir_verify.c", "function": "mir_verify_dominance",
                  "expression": "start == 0", "outcome": "true",
                  "classification": "review-example", "evidence": "synthetic annotation control"}
        source = (coverage.ROOT / review["source"]).read_text().splitlines()
        line = next(index + 1 for index, text in enumerate(source) if review["expression"] in text)
        gap = {"source": review["source"], "function": review["function"], "line": line,
               "column": source[line - 1].index(review["expression"]) + 1,
               "outcome": "true", "review": "unreviewed"}
        gaps = {"uncovered_branch_outcomes": [gap]}
        coverage.annotate_reviews(gaps, [review])
        self.assertEqual(len(gaps["uncovered_branch_outcomes"]), 1)
        self.assertEqual(gap["review"], "review-example")
        with self.assertRaisesRegex(ValueError, "stale or unsupported"):
            coverage.annotate_reviews(gaps, [dict(review, expression="missing expression")])


if __name__ == "__main__":
    unittest.main()