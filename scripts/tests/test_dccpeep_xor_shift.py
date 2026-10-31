"""Exhaustive Z80 machine-state checks for saved-word XOR/byte shifts."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ("dccpeep", "m80c", "l80c")


@unittest.skipUnless(all((ROOT / tool).is_file() for tool in TOOLS)
                     and shutil.which("ntvcm"), "built tools and ntvcm required")
class XorByteShiftTests(unittest.TestCase):
    def test_all_word_inputs_preserve_hl_de_and_af(self):
        harness = """
 cseg
 ld hl,0
Z_LOOP:
 ld (Z_INPUT),hl
 ld de,42330
 call _raw
 ld (Z_WANT_HL),hl
 ld (Z_WANT_DE),de
 push af
 pop bc
 ld (Z_WANT_AF),bc
 ld hl,(Z_INPUT)
 ld de,42330
 call _optimized
 ld (Z_GOT_HL),hl
 ld (Z_GOT_DE),de
 push af
 pop bc
 ld hl,(Z_WANT_AF)
 or a
 sbc hl,bc
 jp nz,Z_FAIL
 ld hl,(Z_WANT_HL)
 ld de,(Z_GOT_HL)
 or a
 sbc hl,de
 jp nz,Z_FAIL
 ld hl,(Z_WANT_DE)
 ld de,(Z_GOT_DE)
 or a
 sbc hl,de
 jp nz,Z_FAIL
 ld hl,(Z_INPUT)
 inc hl
 ld a,h
 or l
 jp nz,Z_LOOP
 ld de,Z_PASS
 jp Z_PRINT
Z_FAIL:
 ld de,Z_BAD
Z_PRINT:
 ld c,9
 call 5
 jp 0
Z_INPUT: dw 0
Z_WANT_HL: dw 0
Z_WANT_DE: dw 0
Z_WANT_AF: dw 0
Z_GOT_HL: dw 0
Z_GOT_DE: dw 0
Z_PASS: db 'PASS xor machine state$'
Z_BAD: db 'FAIL xor machine state$'
"""
        with tempfile.TemporaryDirectory(prefix="dcc-xor-state-") as directory:
            path = Path(directory)
            for mode in ("-Ot", "-Os"):
                for right, count in ((False, 8), *((True, n) for n in range(8, 16))):
                    for dead_de in (False, True):
                        with self.subTest(mode=mode, right=right, count=count,
                                          dead_de=dead_de):
                            body = ("\tpush hl\n" +
                                    ("\tld l,h\n\tld h,0\n" +
                                     "\tsrl l\n" * (count - 8) if right else
                                     "\tld h,l\n\tld l,0\n") +
                                    "\tex de,hl\n\tpop hl\n"
                                    "\tld a,h\n\txor d\n\tld h,a\n"
                                    "\tld a,l\n\txor e\n\tld l,a\n" +
                                    ("\tld de,0\n" if dead_de else "") + "\tret\n")
                            (path / "INPUT.MAC").write_text(
                                "_optimized:\n" + body, encoding="ascii")
                            result = subprocess.run(
                                [str(ROOT / "dccpeep"), mode, "INPUT.MAC", "OPT.MAC"],
                                cwd=path, capture_output=True, text=True, timeout=30)
                            self.assertEqual(result.returncode, 0,
                                             result.stdout + result.stderr)
                            optimized = (path / "OPT.MAC").read_text(encoding="ascii")
                            self.assertIn("fold_xor_byte_shift", optimized)
                            (path / "TEST.MAC").write_text(
                                harness + "_raw:\n" + body + optimized + "\tend\n",
                                encoding="ascii")
                            for command in (
                                    [str(ROOT / "m80c"), "=TEST.MAC", "/X", "/O", "/Z"],
                                    [str(ROOT / "l80c"), "/P:100,TEST", "-o", "TEST"]):
                                result = subprocess.run(
                                    command, cwd=path, capture_output=True,
                                    text=True, timeout=30)
                                self.assertEqual(result.returncode, 0,
                                                 result.stdout + result.stderr)
                            result = subprocess.run(
                                [shutil.which("ntvcm"), "-p", "-s:0", "TEST.COM"],
                                cwd=path, capture_output=True, timeout=30)
                            self.assertEqual(result.returncode, 0,
                                             result.stdout + result.stderr)
                            self.assertIn(b"PASS xor machine state", result.stdout)


if __name__ == "__main__":
    unittest.main()
