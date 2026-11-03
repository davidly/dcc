"""Check campaign assembly baselines before the exhaustive mutation phase."""

import concurrent.futures
import hashlib
import importlib.util
import inspect
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPILER = ROOT / ("dcc.exe" if os.name == "nt" else "dcc")


def load_script(filename):
    path = ROOT / "scripts" / filename
    name = "campaign_baseline_" + path.stem.replace("-", "_")
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class CampaignSelectionTests(unittest.TestCase):
    def test_rejection_guards_survive_later_cost_alternatives(self):
        controls = (
            ("catalan-wave60-audit.py", "main", "catalan-driver-schedule", "zero-calls"),
            ("gnarly-runner-wave100-audit.py", "main", "gnarly-call-runner", "operations"),
            ("indexed-member-write-wave260-audit.py", "indexed_member_write",
             "indexed-member-write", "adjustment"),
        )
        for script, function, template, expected in controls:
            with self.subTest(script=script):
                module = load_script(script)
                parser = (getattr(module, "reject_reason_from", None) or
                          module.reject_reason)
                arguments = (function,) if "function" in inspect.signature(parser).parameters else ()
                report = (
                    f"; MIR machine function={function} template={template} reject={expected}\n"
                    f"; MIR machine function={function} template={template} reject=opcode\n"
                )
                self.assertEqual(parser(report, *arguments, expected=expected), expected)
                self.assertEqual(parser(report, *arguments), "opcode")
                # A missing guard must still fail the caller's equality check.
                self.assertNotEqual(parser(report, *arguments, expected="missing"), "missing")
                self.assertIsNone(parser("", *arguments, expected=expected))
                for other in (report.replace(f"function={function}", "function=other"),
                              report.replace(f"template={template}", "template=other")):
                    self.assertIsNone(parser(other, *arguments, expected=expected))

    def test_scope_block_uses_final_cost_selection(self):
        module = load_script("scope-block-wave1700-audit.py")
        alternatives = (
            "; MIR machine function=main template=scope-block-runner accept=emitted\n"
            "; MIR machine function=main template=scope-block-runner reject=constant\n"
        )
        winner = (
            "; MIR cost-selected function=main candidate=exact-scheduled "
            "selector=scheduled-machine-cfg selected-hash=b3ab4a13\n"
            "; MIR selection function=main selector=scheduled-machine-cfg result=mir\n"
        )
        module.require_exact(alternatives + winner, "exact incumbent")
        with self.assertRaisesRegex(RuntimeError, "did not retain the exact schedule"):
            module.require_exact(alternatives + winner +
                                 "; MIR selection function=main "
                                 "selector=spilled-scalar-cfg result=mir\n", "generic winner")
        with self.assertRaisesRegex(RuntimeError, "did not cost-select"):
            module.require_exact(alternatives +
                                 "; MIR selection function=main "
                                 "selector=scheduled-machine-cfg result=mir\n", "missing cost")


@unittest.skipUnless(COMPILER.is_file(), "built dcc required")
class MirCampaignBaselineTests(unittest.TestCase):
    def test_displaced_aggregate_type_probes_reject(self):
        controls = (
            ("symbol-find-wave21-audit.py", (12, 14, 39, 41, 52, 54, 61, 63, 68, 70)),
            ("symbol-insert-wave22-audit.py", (18, 20, 29, 31, 42, 44, 49, 51, 56, 58, 62, 64)),
        )
        with tempfile.TemporaryDirectory(prefix="dcc-aggregate-probes-") as directory:
            for script, instructions in controls:
                with self.subTest(script=script):
                    module = load_script(script)
                    work = Path(directory) / script
                    work.mkdir()
                    opcodes, baseline = module.baseline(ROOT, COMPILER, work / "baseline.MAC")
                    def check(instruction):
                        return module.mutate(ROOT, COMPILER, work, baseline, opcodes,
                                             (instruction, "type", 912))
                    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                        results = list(pool.map(check, instructions))
                    self.assertTrue(all(row[4] == "rejected" for row in results), results)

    def test_campaign_assembly_baselines(self):
        scheduler = load_script("compiler-coverage-campaigns.py")
        controls = []
        for campaign in scheduler.CAMPAIGNS:
            module = load_script(campaign.script)
            digest = getattr(module, "BASELINE_SHA256", "")
            command = (getattr(module, "compiler_command", None) or
                       getattr(module, "compile_command", None))
            if not re.fullmatch(r"[0-9a-f]{64}", digest) or command is None:
                continue
            try:
                inspect.signature(command).bind(COMPILER, Path("control.mac"))
            except TypeError:
                continue
            controls.append((campaign.name, command, digest))
        self.assertGreater(len(controls), 50, "Campaign baseline discovery regressed")
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith("DCC_MIR_")}

        def check(control):
            name, command, expected = control
            with tempfile.TemporaryDirectory(prefix="dcc-campaign-baseline-") as directory:
                output = Path(directory) / "baseline.MAC"
                result = subprocess.run(command(COMPILER, output), cwd=ROOT,
                                        env=environment, capture_output=True,
                                        text=True, timeout=60)
                if result.returncode or not output.is_file():
                    return (name, result.returncode, result.stderr)
                actual = hashlib.sha256(output.read_bytes()).hexdigest()
                if actual != expected:
                    return (name, f"assembly SHA256 {actual} != {expected}")
                return None

        with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 1)) as pool:
            failures = [result for result in pool.map(check, controls) if result]
        self.assertEqual(failures, [])


if __name__ == "__main__":
    unittest.main()
