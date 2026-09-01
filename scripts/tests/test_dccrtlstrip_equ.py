import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "src/dccrtlstrip/dccrtlstrip.c"
APP_STRIP_SOURCE = REPO_ROOT / "src/dccrtlstrip/dcc_app_strip.c"
BUILD_DIR = REPO_ROOT / "build"
CC = shutil.which("clang") or shutil.which("gcc")


@unittest.skipUnless(CC, "host C compiler is required")
class DccRtlStripEquTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        BUILD_DIR.mkdir(exist_ok=True)

    def test_equ_dependency_keeps_fallthrough_public_block(self):
        with tempfile.TemporaryDirectory(
            prefix="test-dccrtlstrip-equ-", dir=BUILD_DIR
        ) as temporary:
            work = Path(temporary)
            tool = work / "dccrtlstrip"
            runtime = work / "DCCRTL.MAC"
            app = work / "APP.MAC"
            output = work / "RTLMIN.MAC"

            subprocess.run(
                [
                    CC,
                    "-std=c89",
                    "-O2",
                    str(SOURCE),
                    str(APP_STRIP_SOURCE),
                    "-o",
                    str(tool),
                ],
                check=True,
                cwd=REPO_ROOT,
            )
            runtime.write_text(
                "        cseg\n"
                "        public  start\n"
                "start:\n"
                "        ret\n"
                "        public  _general\n"
                "_general:\n"
                "        ld      hl,1\n"
                "DFAST   equ     __fast\n"
                "        public  __fast\n"
                "__fast:\n"
                "        inc     hl\n"
                "        ret\n"
                "        public  _dead\n"
                "_dead:\n"
                "        ret\n"
                "        end     start\n"
            )
            app.write_text("        extrn   _general\n")

            result = subprocess.run(
                [
                    str(tool),
                    "-r",
                    str(runtime),
                    "-o",
                    str(output),
                    str(app),
                ],
                cwd=work,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            stripped = output.read_text()
            self.assertIn("_general:", stripped)
            self.assertIn("__fast:", stripped)
            self.assertNotIn("_dead:", stripped)


if __name__ == "__main__":
    unittest.main()
