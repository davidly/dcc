"""Exercise data-only clobber inventory and isolated process-shard contracts."""

from pathlib import Path
import shutil
import subprocess
import unittest


@unittest.skipUnless(shutil.which("pwsh"), "PowerShell 7 is required")
class MirClobberRunnerTests(unittest.TestCase):
    def test_runner_contracts(self):
        root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            ["pwsh", "-NoProfile", "-File", "scripts/tests/test_mir_clobber_runner.ps1"],
            cwd=root,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=120,
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("runner harness passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
