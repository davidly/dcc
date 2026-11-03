"""Catch exact-schedule regressions before the long coverage/runtime campaign."""

import concurrent.futures
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPILER = ROOT / ("dcc.exe" if os.name == "nt" else "dcc")


@unittest.skipUnless(COMPILER.is_file(), "built dcc required")
class MirExactControlTests(unittest.TestCase):
    def test_campaign_positive_controls_select_their_exact_schedules(self):
        cases = []
        for path in sorted((ROOT / "scripts/mir-clobber-cases").glob("*.json")):
            for case in json.loads(path.read_text(encoding="utf-8")):
                if (case.get("RequireExact") and len(case.get("Sources", [])) == 1
                        and not case.get("MachineMutation")):
                    cases.append(case)
        self.assertTrue(cases, "No positive exact controls discovered")
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith("DCC_MIR_")}
        environment.update(DCC_MIR_REQUIRE_COMPLETE="1", DCC_MIR_REQUIRE_EMIT="1",
                           DCC_MIR_MACHINE_REPORT="1")

        def check(case):
            with tempfile.TemporaryDirectory(prefix="dcc-exact-control-") as directory:
                command = [str(COMPILER), "-stack", str(case.get("StackBytes", 512)),
                           "-I", str(ROOT),
                           *[f"-D{value}" for value in case.get("Defines", [])],
                           str(ROOT / case["Sources"][0]),
                           "-o", str(Path(directory) / "control.mac")]
                completed = subprocess.run(command, cwd=ROOT, env=environment,
                                           capture_output=True, text=True, timeout=60)
                function = re.escape(case["ExactFunction"]) if case.get("ExactFunction") else r"\S+"
                pattern = (rf"MIR machine function={function} "
                           rf"template={re.escape(case['ExactTemplate'])} accept=emitted\b")
                if completed.returncode or not re.search(pattern, completed.stderr):
                    return (case["Name"], completed.returncode, completed.stderr)
                return None

        with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 1)) as pool:
            failures = [result for result in pool.map(check, cases) if result]
        self.assertEqual(failures, [])


if __name__ == "__main__":
    unittest.main()
