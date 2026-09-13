"""Verify the independent target-width oracle for the Wave 32 PHI/alias proof."""

import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/mir-clobber/phial32.c"
CAMPAIGN = ROOT / "scripts/mir-clobber-cases/phi-alias-wave32.json"


def word_result(target, other, choose, delta, aliases):
    selected = (target + 0x1234) & 0xFFFF if choose else target ^ 0x55AA
    live_a = (selected + 0x0101) & 0xFFFF
    live_b = selected ^ 0xA55A
    live_c = (selected * 3) & 0xFFFF
    if aliases:
        target = (target + delta) & 0xFFFF
    else:
        other = (other + delta) & 0xFFFF
    result = (
        selected + live_a + live_b + live_c + target + target
    ) & 0xFFFF
    return result, target, other


def byte_result(target, other, choose, delta, aliases):
    selected = (target + 0x34) & 0xFF if choose else target ^ 0xAA
    live_a = (selected + 0x0101) & 0xFFFF
    live_b = selected ^ 0xA55A
    live_c = (selected * 3) & 0xFFFF
    if aliases:
        target = (target + delta) & 0xFF
    else:
        other = (other + delta) & 0xFF
    result = (
        selected + live_a + live_b + live_c + target + target
    ) & 0xFFFF
    return result, target, other


class PhiAliasWave32Tests(unittest.TestCase):
    def test_target_width_oracle_and_campaign(self):
        cases = (
            ("word-alias-true", word_result(0x1020, 0x3040, True, 7, True)),
            ("word-disjoint-false", word_result(0x1020, 0x3040, False, 9, False)),
            ("word-wrap-false", word_result(0xFFFF, 0x2222, False, 1, True)),
            ("word-disjoint-true", word_result(0, 0x8000, True, 0xFFFF, False)),
            ("byte-alias-true", byte_result(0xF0, 0x31, True, 0x30, True)),
            ("byte-disjoint-false", byte_result(0x20, 0x40, False, 9, False)),
            ("byte-wrap-false", byte_result(0xFF, 0x22, False, 1, True)),
            ("byte-disjoint-true", byte_result(0, 0x80, True, 0xFF, False)),
        )
        expected = {name: result for name, (result, _, _) in cases}
        source_expected = {
            name: int(value)
            for name, value in re.findall(
                r'check\("([^"]+)", actual, (\d+)U,', SOURCE.read_text()
            )
        }
        self.assertEqual(source_expected, expected)

        oracle_hash = 23117
        for checks, (_, (actual, target, other)) in enumerate(cases, 1):
            oracle_hash = (
                oracle_hash * 109
                + actual
                + target * 3
                + other * 5
                + checks
            ) & 0xFFFF
        self.assertEqual(oracle_hash, 65265)

        campaign = {case["Name"]: case for case in json.loads(CAMPAIGN.read_text())}
        self.assertEqual(set(campaign), {"pa32word", "pa32byte", "pa32fault"})
        for name, function in (
            ("pa32word", "phi_alias_word"),
            ("pa32byte", "phi_alias_byte"),
        ):
            self.assertIn(
                f"PHIAL32 checks=8 failures=0 hash={oracle_hash}",
                campaign[name]["Expected"],
            )
            self.assertEqual(
                campaign[name]["RequiredSelector"], "spilled-scalar-cfg"
            )
            self.assertEqual(campaign[name]["RequiredSelectorFunction"], function)
            self.assertEqual(campaign[name]["StackModes"], [True, False])
            self.assertEqual(campaign[name]["DebugModes"], ["true", "lines"])

        fault = campaign["pa32fault"]
        self.assertEqual(fault["Defines"], ["PHI_ALIAS_FAULT=1"])
        self.assertEqual(fault["Exit"], len(cases))
        self.assertIn(
            f"FAIL word-alias-true got={expected['word-alias-true'] ^ 1} "
            f"expected={expected['word-alias-true']}",
            fault["Expected"],
        )
        self.assertIn(
            f"FAIL byte-disjoint-true got={expected['byte-disjoint-true'] ^ 1} "
            f"expected={expected['byte-disjoint-true']}",
            fault["Expected"],
        )


if __name__ == "__main__":
    unittest.main()
