import importlib.util
from pathlib import Path
import unittest


SPEC = importlib.util.spec_from_file_location(
    "pointer_condition_wave25_audit",
    Path(__file__).resolve().parents[1] / "pointer-condition-wave25-audit.py",
)
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class PointerConditionWave25AuditTests(unittest.TestCase):
    def test_label_immediate_is_unused(self):
        result = audit.classify(
            (0, "label", "immediate", 0, "accepted", "")
        )
        self.assertEqual(result[-1], "unused-opcode-field")


if __name__ == "__main__":
    unittest.main()
