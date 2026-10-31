import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
EMULATOR = shutil.which("ntvcm")
TOOLS = ("dcc", "dccmake", "dccpeep", "dccrtlstrip", "m80c", "l80c")

SOURCE = ROOT / "tests" / "tmeminfo.c"


@unittest.skipUnless(EMULATOR and all((ROOT / tool).is_file() for tool in TOOLS),
                     "built DCC tools and ntvcm are required")
class MemoryInfoTests(unittest.TestCase):
    def test_ordinary_com_snapshots_with_different_stack_reserves(self):
        with tempfile.TemporaryDirectory(prefix="dcc meminfo ") as directory:
            folder = Path(directory)
            (folder / "MEMINFO.C").write_text(SOURCE.read_text(encoding="ascii"),
                                            encoding="ascii")
            settings = {
                "dcc-input": "MEMINFO.C", "dcc-output": "MEMINFO",
                "dcc-include-directory": str(ROOT),
                "dcc-runtime": str(ROOT / "DCCRTL.MAC"),
                **{f"{tool}-tool": str(ROOT / tool) for tool in TOOLS
                   if tool != "dccmake"},
            }
            (folder / "dccmake.txt").write_text(
                "".join(f"{key}={value}\n" for key, value in settings.items()))
            for stack in (256, 512, 2048):
                with self.subTest(stack=stack):
                    result = subprocess.run(
                        [str(ROOT / "dccmake"), f"dcc-stack-bytes={stack}",
                         f"dcc-define=EXPECT_STACK={stack},MEMINFO_VERBOSE=1"],
                        cwd=folder, capture_output=True, text=True,
                        env={**os.environ, "DCC_ARGS": ""}, timeout=60)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    result = subprocess.run(
                        [EMULATOR, "build/MEMINFO.COM"], cwd=folder,
                        capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("PASS: meminfo bounds", result.stdout)
                    symbols = (folder / "build/MEMINFO.SYM").read_text()
                    heap_start = re.search(r"\b([0-9A-F]{4})\s+__HSTA\b", symbols)
                    self.assertIsNotNone(heap_start, symbols)
                    start = int(heap_start.group(1), 16)
                    self.assertIn(f"MEMINFO start={start} break={start} ", result.stdout)


if __name__ == "__main__":
    unittest.main()
