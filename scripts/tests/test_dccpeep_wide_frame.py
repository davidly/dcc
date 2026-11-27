"""Safety gates for wide-frame rewrites and leaf argument returns."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless((ROOT / "dccpeep").is_file(), "built dccpeep required")
class WideFrameTests(unittest.TestCase):
    def optimize(self, body, marker=";@dcc.frame-cacheable\n"):
        source = ("; dcc stage-1d output\ncseg\n;@dcc.lto begin _leaf\n"
                  "public _leaf\n_leaf:\n" + marker +
                  "push ix\nld ix,0\nadd ix,sp\n" + body +
                  "\n;@dcc.lto end _leaf\nend\n")
        with tempfile.TemporaryDirectory(prefix="dcc-wide-frame-") as directory:
            folder = Path(directory)
            (folder / "IN.MAC").write_text(source, encoding="ascii")
            result = subprocess.run([str(ROOT / "dccpeep"), "-closed-program",
                                     str(folder / "IN.MAC"), str(folder / "OUT.MAC")],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return (folder / "OUT.MAC").read_text(encoding="ascii")

    def shift(self, operation, live=False):
        return ("ld l,(ix-6)\nld h,(ix-5)\nld e,(ix-8)\nld d,(ix-7)\n" +
                operation + "\nld (ix-6),l\nld (ix-5),h\nld (ix-8),e\nld (ix-7),d\n" +
                "ld a,(ix-6)\nor (ix-5)\nor (ix-8)\nor (ix-7)\nld (_sink),a\n" +
                ("" if live else "ld hl,123\nld de,456\n") + "pop ix\nret")

    def test_in_place_shift_keeps_discontiguous_byte_order(self):
        for operation, expected in (
                ("add hl,hl\nrl e\nrl d", ("sla (ix-6)", "rl (ix-5)", "rl (ix-8)", "rl (ix-7)")),
                ("srl d\nrr e\nrr h\nrr l", ("srl (ix-7)", "rr (ix-8)", "rr (ix-5)", "rr (ix-6)")),
                ("sra d\nrr e\nrr h\nrr l", ("sra (ix-7)", "rr (ix-8)", "rr (ix-5)", "rr (ix-6)"))):
            with self.subTest(operation=operation):
                output = self.optimize(self.shift(operation))
                self.assertIn("wide_frame_shift", output)
                positions = [output.index(instruction) for instruction in expected]
                self.assertEqual(positions, sorted(positions))

    def test_shift_declines_live_results_volatile_or_unowned_frames(self):
        operation = "add hl,hl\nrl e\nrl d"
        self.assertNotIn("wide_frame_shift", self.optimize(self.shift(operation, live=True)))
        for marker in ("", ";@dcc.frame-cacheable\n;@dcc.volatile-local\n"):
            self.assertNotIn("wide_frame_shift", self.optimize(self.shift(operation), marker))

    def test_mask_reload_keeps_only_selected_byte_when_results_are_dead(self):
        body = ("ld l,(ix+4)\nld h,(ix+5)\nld e,(ix+6)\nld d,(ix+7)\n"
                "ld a,h\nand 128\njr z,L1\nld (_sink),a\nL1:\n"
                "ld hl,12\nld de,34\npop ix\nret")
        output = self.optimize(body)
        # Other passes may first remove the dead loads and then the frame.
        # Either lowering must read only byte 1 and retain the mask/branch.
        self.assertIn("and 128", output)
        self.assertNotIn("ld e,(ix+6)", output)
        self.assertNotIn("ld d,(ix+7)", output)
        self.assertTrue("ld a,(ix+5)" in output or "ld hl,3" in output)
        self.assertNotIn("wide_frame_mask_test", self.optimize(body.replace("ld hl,12\n", "")))
        self.assertNotIn("wide_frame_mask_test", self.optimize(body, ""))
        self.assertNotIn("wide_frame_mask_test", self.optimize(
            body, ";@dcc.frame-cacheable\n;@dcc.volatile-parameter\n"))

    def test_leaf_argument_return_removes_frame_without_moving_side_effect(self):
        body = ("ld hl,(_calls)\ninc hl\nld (_calls),hl\n"
                "ld l,(ix+4)\nld h,(ix+5)\nld h,0\npop ix\nret")
        output = self.optimize(body)
        self.assertIn("leaf_return_argument", output)
        self.assertNotIn("push ix", output)
        self.assertLess(output.index("ld (_calls),hl"), output.index("leaf_return_argument"))
        self.assertIn("ld h,0", output)
        self.assertNotIn("leaf_return_argument", self.optimize(body.replace("inc hl", "call _change")))
        self.assertNotIn("leaf_return_argument", self.optimize(body, ""))


if __name__ == "__main__":
    unittest.main()
