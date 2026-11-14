"""Required MIR diagnostics must survive suppressed parser probes."""

import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("run_known_bugs", ROOT / "scripts/run-known-bugs.py")
KNOWN_BUGS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(KNOWN_BUGS)


class DiagnosticVerdictTests(unittest.TestCase):
    def test_diagnostic_verdict_requires_failure_and_matching_message(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build", prefix="diagnostic-verdict-") as directory:
            path = Path(directory)
            source = path / "probe.c"
            source.write_text("/*\n * DCC-EXPECT-ERROR: MIR emission is required\n */\n")
            args = argparse.Namespace(dcc=None, emulator="ntvcm", timeout=3)
            for peep in (False, True):
                for status, message, expected in (
                    (1, "dcc: fatal: MIR emission is required", "pass"),
                    (0, "dcc: fatal: MIR emission is required", "missing"),
                    (1, "dcc: fatal: unrelated failure", "missing"),
                    (1, "source text: MIR emission is required\n"
                        "dcc: fatal: unrelated failure", "missing"),
                    (1, "probe.c:1: error DCC-E1002: unsupported expression near "
                        "'MIR emission is required'", "missing"),
                    (1, "; MIR emission is required\ndcc: fatal: unrelated failure", "missing"),
                    (1, "probe.c:1: error DCC-E1002: MIR emission is required near 'x'", "pass"),
                ):
                    with self.subTest(peep=peep, status=status, message=message):
                        result = subprocess.CompletedProcess([], status, "", message)
                        with mock.patch.object(KNOWN_BUGS.subprocess, "run", return_value=result):
                            verdict = KNOWN_BUGS.dcc_verdict(source, path, peep, "", args)
                        self.assertTrue(verdict.startswith(expected), verdict)


@unittest.skipUnless((ROOT / "dcc").is_file(), "built dcc required")
class MirDiagnosticsTests(unittest.TestCase):
    def test_reused_label_does_not_mask_later_required_emission_failure(self):
        fixture = ROOT / "tests/diagnostics/fixtures/mir-required-after-reused-label.c"
        baseline = fixture.with_suffix(".txt")
        source = fixture.read_text()
        variants = (source, source.replace("{ { L1:; }", "{ { L2:; }"),
                    source.replace("{ { L1:; }", "{ L1:;"))
        with tempfile.TemporaryDirectory(dir=ROOT / "build", prefix="mir-diagnostic-") as directory:
            path = Path(directory)
            input_path = path / "probe.c"
            for flags in (["-O0"], ["-O2"], KNOWN_BUGS.SANITIZE):
                output, error = KNOWN_BUGS.host_output(fixture, path, flags, 30)
                self.assertIsNone(error)
                self.assertEqual(output, baseline.read_text())
            for strict in (False, True):
                environment = dict(os.environ)
                for name in ("DCC_MIR_REQUIRE_COMPLETE", "DCC_MIR_REQUIRE_EMIT"):
                    if strict:
                        environment[name] = "1"
                    else:
                        environment.pop(name, None)
                for variant in variants:
                    input_path.write_text(variant)
                    for options in ((), ("-fstack-check",), ("-gline",), ("-g",)):
                        with self.subTest(strict=strict, variant=variant, options=options):
                            result = subprocess.run(
                                [str(ROOT / "dcc"), "-I", str(ROOT), *options,
                                 str(input_path), "-o", str(path / "PROBE.MAC")],
                                env=environment, capture_output=True, text=True, timeout=30,
                            )
                            if options == ("-g",):
                                # Full-debug selection can emit this BUG-020 trigger.
                                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                            else:
                                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                                expected = ("DCC_MIR_REQUIRE_EMIT requires MIR emission"
                                            if strict else "MIR emission is required")
                                self.assertIn(expected, result.stderr)
