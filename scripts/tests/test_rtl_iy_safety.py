import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOL = REPO_ROOT / "scripts/rtl-iy-safety.py"

VALID_RUNTIME = """\
__extln:
        push    iy
        push    hl
        pop     iy
        pop     iy
        pop     ix
        ret
__zerdm:
_setjmp:
        push    iy
        pop     de
        ld      a,e
        ld      (hl),a
        inc     hl
        ld      a,d
        ld      (hl),a
        ret
_longjmp:
        ld      a,b
        or      c
        jr      nz,lj_valok
        ld      bc,1
lj_valok:
        ld      c,(hl)
        inc     hl
        ld      b,(hl)
        push    bc
        pop     iy
        ld      sp,hl
        ret
_dopn:
"""


class RtlIySafetyTests(unittest.TestCase):
    def run_audit(self, runtime_text):
        with tempfile.TemporaryDirectory(prefix="test-rtl-iy-safety-") as temporary:
            runtime = Path(temporary) / "DCCRTL.MAC"
            runtime.write_text(runtime_text)
            return subprocess.run(
                [sys.executable, str(TOOL), str(runtime)],
                cwd=temporary,
                text=True,
                capture_output=True,
            )

    def test_reviewed_regions_pass(self):
        result = self.run_audit(VALID_RUNTIME)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("confined to audited runtime paths", result.stdout)

    def test_unexpected_helper_fails(self):
        result = self.run_audit(VALID_RUNTIME + "_other:\n        push    iy\n")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("REVIEW:", result.stdout)

    def test_missing_setjmp_save_fails(self):
        result = self.run_audit(
            VALID_RUNTIME.replace("_setjmp:\n        push    iy\n", "_setjmp:\n")
        )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("REVIEW:", result.stdout)

    def test_early_setjmp_return_fails(self):
        result = self.run_audit(
            VALID_RUNTIME.replace("_setjmp:\n", "_setjmp:\n        ret\n")
        )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("REVIEW:", result.stdout)

    def test_longjmp_restore_bypass_fails(self):
        result = self.run_audit(
            VALID_RUNTIME.replace(
                "jr      nz,lj_valok",
                "jr      nz,lj_after_restore",
            ).replace(
                "        pop     iy\n        ld      sp,hl\n",
                "        pop     iy\nlj_after_restore:\n        ld      sp,hl\n",
            )
        )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("REVIEW:", result.stdout)

    def test_late_longjmp_restore_fails(self):
        result = self.run_audit(
            VALID_RUNTIME.replace(
                "        pop     iy\n        ld      sp,hl\n",
                "        ld      sp,hl\n        pop     iy\n",
            )
        )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("REVIEW:", result.stdout)


if __name__ == "__main__":
    unittest.main()
