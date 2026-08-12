import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOL = REPO_ROOT / "scripts/audit-c-module-exports.py"
BUILD_DIR = REPO_ROOT / "build"
CC = shutil.which("clang") or shutil.which("gcc") or shutil.which("cl")
NM = shutil.which("llvm-nm") or shutil.which("nm")


@unittest.skipUnless(CC and NM, "host C compiler and nm/llvm-nm are required")
class AuditCModuleExportsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build_dir_existed = BUILD_DIR.exists()
        BUILD_DIR.mkdir(exist_ok=True)

    @classmethod
    def tearDownClass(cls):
        if not cls.build_dir_existed:
            try:
                BUILD_DIR.rmdir()
            except OSError:
                pass

    def run_audit(self, source_text, *arguments):
        with tempfile.TemporaryDirectory(
            prefix="test-audit-c-module-exports-", dir=BUILD_DIR
        ) as temporary:
            source = Path(temporary) / "module.c"
            source.write_text(source_text)
            return subprocess.run(
                [
                    sys.executable,
                    str(TOOL),
                    "--cc",
                    CC,
                    "--nm",
                    NM,
                    str(source),
                    *arguments,
                ],
                cwd=temporary,
                text=True,
                capture_output=True,
            )

    def test_static_only_module_passes(self):
        result = self.run_audit("static int helper(void) { return 42; }\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("static top-level functions (best effort): 1", result.stdout)
        self.assertIn("exported functions: (none)", result.stdout)
        self.assertIn("result: PASS", result.stdout)

    def test_allowlisted_dispatch_passes(self):
        result = self.run_audit(
            "static int helper(void) { return 42; }\n"
            "int module_dispatch(void) { return helper(); }\n",
            "--allow-function",
            "module_dispatch",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("module_dispatch [T]", result.stdout)
        self.assertIn("result: PASS", result.stdout)

    def test_writable_global_fails(self):
        result = self.run_audit("int shared_counter = 1;\n")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("writable data: shared_counter [D]", result.stdout)
        self.assertIn("zero-shared-state policy", result.stdout)

    def test_unexpected_function_fails(self):
        result = self.run_audit("int accidental_export(void) { return 0; }\n")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("unexpected exported function accidental_export", result.stdout)
        self.assertIn("--allow-function accidental_export", result.stdout)


if __name__ == "__main__":
    unittest.main()
