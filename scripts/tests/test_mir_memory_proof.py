import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest


MODULE = Path(__file__).resolve().parents[1] / "audit-mir-memory-proof.py"
SPEC = importlib.util.spec_from_file_location("memory_proof", MODULE)
PROOF = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROOF)


class MemoryProofTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.source = Path(self.directory.name) / "dcc_mir.c"
        self.source.write_text("guard\n")
        self.inventory = {
            "dominated_cases": ["dom"], "endian_cases": ["end"],
            "mandatory_branch_guards": {"rewrite": ["guard"]},
            "mutant_assertions": {"new": "end"},
        }
        self.report = {
            "type": "llvm.coverage.json.export", "data": [{
                "functions": [{
                    "name": "dcc_mir.c:rewrite", "filenames": [str(self.source)],
                    "branches": [[1, 1, 1, 6, 2, 3]],
                }],
            }],
        }
        self.log = ("; MIR memory-proof case=dom outcome=passed\n"
                    "; MIR memory-proof case=end outcome=passed\n"
                    "MIR verifier failures=0\n")
        self.prior = [{"mutation": "baseline", "outcome": "passed", "exitCode": 0},
                      {"mutation": "old", "outcome": "killed", "exitCode": 1}]
        self.results = self.prior + [{"mutation": "new", "outcome": "killed", "exitCode": 1}]
        self.logs = {"new": "FAIL memory rewrite end\n"
                     "; MIR memory-proof case=end outcome=failed\n"
                     "MIR verifier failures=1\n"}

    def run_audit(self):
        return PROOF.audit(self.inventory, self.report, self.source, self.log,
                           self.results, self.prior, self.logs)

    def test_positive(self):
        self.assertEqual(self.run_audit()["cases"], 2)

    def test_missing_duplicate_wrong_or_failed_case(self):
        original = self.log
        for bad in ("", original + original, original.replace("dom", "other"),
                    original.replace("outcome=passed", "outcome=failed"),
                    original.replace("failures=0", "failures=1")):
            with self.subTest(log=bad):
                self.log = bad
                with self.assertRaises(ValueError):
                    self.run_audit()

    def test_missing_wrong_or_duplicate_function(self):
        original = copy.deepcopy(self.report)
        for kind in ("missing", "wrong-name", "wrong-source", "duplicate"):
            self.report = copy.deepcopy(original)
            functions = self.report["data"][0]["functions"]
            if kind == "missing":
                functions.clear()
            elif kind == "wrong-name":
                functions[0]["name"] = "wrong"
            elif kind == "wrong-source":
                functions[0]["filenames"] = ["other.c"]
            else:
                functions.append(copy.deepcopy(functions[0]))
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                self.run_audit()

    def test_missing_guard_or_unexecuted_outcome(self):
        branch = self.report["data"][0]["functions"][0]["branches"][0]
        for column in (4, 5):
            saved = branch[column]
            branch[column] = 0
            with self.assertRaises(ValueError):
                self.run_audit()
            branch[column] = saved
        self.inventory["mandatory_branch_guards"]["rewrite"] = ["missing"]
        with self.assertRaises(ValueError):
            self.run_audit()

    def test_mutants_cannot_disappear_survive_or_be_invalid(self):
        original = copy.deepcopy(self.results)
        for kind in ("missing-old", "missing-new", "duplicate", "survived", "invalid"):
            self.results = copy.deepcopy(original)
            if kind == "missing-old":
                self.results.pop(1)
            elif kind == "missing-new":
                self.results.pop()
            elif kind == "duplicate":
                self.results.append(self.results[-1])
            else:
                self.results[-1]["outcome"] = kind
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                self.run_audit()

    def test_branch_coordinates_are_exact_not_whole_lines(self):
        self.assertEqual(PROOF.branch_expression(["left || right"],
                                                [1, 9, 1, 14]), "right")
        self.assertEqual(PROOF.branch_expression(["first(", " second)"],
                                                [1, 1, 2, 9]), "first(\n second)")

    def test_intended_assertion_log_and_exit_are_mandatory(self):
        original = self.logs["new"]
        for bad in ("", original.replace("rewrite end", "rewrite other"),
                    original.replace("case=end", "case=other"),
                    original.replace("failures=1", "failures=0")):
            self.logs["new"] = bad
            with self.assertRaises(ValueError):
                self.run_audit()
        self.logs["new"] = original
        self.results[-1]["exitCode"] = 134
        with self.assertRaises(ValueError):
            self.run_audit()
