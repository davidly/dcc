"""Safety gates for wide-frame rewrites and leaf argument returns."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless((ROOT / "dccpeep").is_file(), "built dccpeep required")
class WideFrameTests(unittest.TestCase):
    def optimize(self, body, marker=";@dcc.frame-cacheable\n", suffix=""):
        source = ("; dcc stage-1d output\ncseg\n;@dcc.lto begin _leaf\n"
                  "public _leaf\n_leaf:\n" + marker +
                  "push ix\nld ix,0\nadd ix,sp\n" + body +
                  "\n;@dcc.lto end _leaf\n" + suffix + "end\n")
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

    def frame_pointer_update(self):
        return ("ld hl,-12\nadd hl,sp\nld sp,hl\n"
                "ld (ix-2),0\nld (ix-1),0\n"
                "push ix\npop hl\ndec hl\ndec hl\nld c,l\nld b,h\n"
                "L1:\nld l,c\nld h,b\npush hl\nld l,c\nld h,b\n"
                "ld a,(hl)\ninc hl\nld h,(hl)\nld l,a\n"
                "ld e,(ix+4)\nld d,(ix+5)\nadd hl,de\n"
                "ld (ix-10),l\nld (ix-9),h\nex de,hl\npop hl\n"
                "ld (hl),e\ninc hl\nld (hl),d\n"
                "ld hl,17\nld de,19\nld (_sink),a\n"
                "ld sp,ix\npop ix\nret")

    def test_frame_pointer_update_keeps_volatile_order_and_live_a(self):
        output = self.optimize(self.frame_pointer_update())
        accesses = [line.strip() for line in output.splitlines()
                    if "@dcc.volatile-access" in line]
        self.assertEqual(accesses, [
            "ld a,(ix-2) ;@dcc.volatile-access",
            "ld h,(ix-1) ;@dcc.volatile-access",
            "ld (ix-2),l ;@dcc.volatile-access",
            "ld (ix-1),h ;@dcc.volatile-access"])
        self.assertIn("ld (_sink),a", output)

    def test_frame_pointer_update_declines_escapes_and_frame_changes(self):
        body = self.frame_pointer_update()
        for bad in (body.replace("L1:", "push bc\ncall _escape\npop bc\nL1:"),
                    body.replace("L1:", "ld ix,123\nL1:"),
                    body.replace("push ix\npop hl\ndec hl", "jp L1\npush ix\npop hl\ndec hl"),
                    body.replace("ld e,(ix+4)", "ld e,(ix+6)"),
                    body.replace("ld hl,17", "ld (_sink),hl"),
                    body.replace("ld de,19", "ld (_sink),de"),
                    body.replace("L1:", "ld (_sink),hl\nL1:")):
            with self.subTest(body=bad):
                self.assertNotIn("@dcc.volatile-access", self.optimize(bad))
        self.assertNotIn("@dcc.volatile-access", self.optimize(body, marker=""))

    def test_scalar_compare_preserves_flags_observed_by_jp_and_jr(self):
        body = ("ld l,(ix+4)\nld h,(ix+5)\nld e,(ix+6)\nld d,(ix+7)\n"
                "push hl\npush de\nor a\nsbc hl,de\npop de\npop hl\n"
                "jr nc,L1\nld hl,1\njr L2\nL1:\nld hl,2\nL2:\n"
                "ld (_sink),hl\nld a,0\nor a\npop ix\nret")
        for jump in ("jp", "jr"):
            with self.subTest(jump=jump):
                source = body.replace("jr ", jump + " ")
                self.assertIn("scalar_word_compare", self.optimize(source))
                for condition in ("z", "nz", "m", "p", "pe", "po"):
                    if jump == "jr" and condition not in ("z", "nz"):
                        continue
                    live = source.replace("L1:\n", "L1:\n" + jump + " " + condition + ",L2\n")
                    self.assertNotIn("scalar_word_compare", self.optimize(live))

    def test_scalar_increment_preserves_observed_carry(self):
        body = ("ld l,(ix+4)\nld h,(ix+5)\nld e,(ix+6)\nld d,(ix+7)\n"
                "ld iy,1\npush de\npush iy\npop de\nadd hl,de\npop de\n"
                "ld iy,0\nL1:\nld (_sink),hl\nld a,0\nor a\npop ix\nret")
        self.assertIn("scalar_word_increment", self.optimize(body))
        for jump in ("jp", "jr"):
            live = body.replace("L1:\n", jump + " c,L1\nL1:\n")
            self.assertNotIn("scalar_word_increment", self.optimize(live))

    def test_mirrored_accumulator_keeps_only_private_bc_uses(self):
        store = "push hl\nld h,b\nld l,c\nld (ix-2),l\nld (ix-1),h\npop hl\n"
        load = "push hl\nld l,(ix-2)\nld h,(ix-1)\nld b,h\nld c,l\npop hl\n"
        body = ("dec sp\ndec sp\nld e,(ix+4)\nld d,(ix+5)\n"
                "ld l,(ix+6)\nld h,(ix+7)\nld bc,0\n" + store +
                "L0:\nld a,h\nor l\njr z,Lend\nld a,l\nand 1\nld c,a\nld b,0\n"
                "push hl\nld h,b\nld l,c\nld a,h\nor l\npop hl\njr z,Lskip\n" + load +
                "ld a,c\nadd a,e\nld c,a\nld a,b\nadc a,d\nld b,a\n" + store +
                "Lskip:\nex de,hl\nadd hl,hl\nex de,hl\nsrl h\nrr l\njp L0\n"
                "Lend:\nld l,(ix-2)\nld h,(ix-1)\nld sp,ix\npop ix\nret")
        output = self.optimize(body)
        self.assertIn("leaf_mirrored_word", output)
        self.assertNotIn("(ix-2)", output)
        self.assertNotIn("(ix-1)", output)
        for unsafe in (body.replace("Lskip:\n", "Lskip:\nld bc,5\n"),
                       body.replace("Lskip:\n", "Lskip:\ninc (ix-2)\n"),
                       body.replace("Lskip:\n", "Lskip:\npush ix\npop de\n")):
            self.assertNotIn("leaf_mirrored_word", self.optimize(unsafe))
        self.assertNotIn("leaf_mirrored_word", self.optimize(
            body, ";@dcc.frame-cacheable\n;@dcc.volatile-local\n"))

    def test_counter_guard_inlines_only_proven_void_loop_calls(self):
        helper = (";@dcc.lto begin _guard\n_guard:\n;@dcc.frame-cacheable\n"
                  ";@dcc.return-void\nld hl,2\nadd hl,sp\nld a,(hl)\ninc hl\n"
                  "ld h,(hl)\nld l,a\nld a,h\nor l\nret nz\n"
                  "ld hl,(_counter)\ninc hl\nld (_counter),hl\nret\n"
                  ";@dcc.lto end _guard\n")
        loop = ("ld de,0\nL0:\nld hl,1\npush hl\ncall _guard\npop bc\n"
                "inc de\nld a,d\ncp 2\njr c,L0\npop ix\nret")
        output = self.optimize(loop, suffix=helper)
        self.assertIn("inline_loop_counter_guard", output)
        self.assertNotIn("call _guard", output)
        # BC must still equal the popped argument after the original call.
        self.assertIn("ld b,h", output)
        self.assertIn("ld c,l", output)
        for unsafe in (helper.replace(";@dcc.return-void\n", ""),
                       helper.replace(";@dcc.return-void", ";@dcc.volatile-parameter\n;@dcc.return-void"),
                       helper.replace("inc hl\nld (_counter)", "dec hl\nld (_counter)")):
            self.assertNotIn("inline_loop_counter_guard", self.optimize(loop, suffix=unsafe))
        self.assertNotIn("inline_loop_counter_guard", self.optimize(
            loop.replace("jr c,L0\n", ""), suffix=helper))

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
