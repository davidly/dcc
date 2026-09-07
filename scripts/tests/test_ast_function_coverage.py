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
        with self.assertRaisesRegex(ValueError, "exact function selection"):
            coverage.summarize_native(text, {"active", "missing"})
        with self.assertRaisesRegex(ValueError, "unexpected or duplicate"):
            coverage.summarize_native(text, {"other"})
        with self.assertRaisesRegex(ValueError, "unexpected or duplicate"):
            coverage.summarize_native(text + text, {"active"})


if __name__ == "__main__":
    unittest.main()