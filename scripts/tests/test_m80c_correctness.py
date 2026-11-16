import os
import shutil
import subprocess
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = REPO_ROOT / "build"
CC = shutil.which("clang") or shutil.which("gcc")


@unittest.skipUnless(CC, "host C compiler is required")
class M80cCorrectnessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = BUILD_DIR / ("test-m80c-correctness-%d" % os.getpid())
        cls.workspace.mkdir(parents=True, exist_ok=True)
        cls.m80c = cls.workspace / "m80c"
        cls.l80c = cls.workspace / "l80c"
        subprocess.run(
            [
                CC,
                "-std=c89",
                "-O2",
                str(REPO_ROOT / "src" / "m80c" / "m80c.c"),
                "-o",
                str(cls.m80c),
            ],
            check=True,
            cwd=REPO_ROOT,
        )
        subprocess.run(
            [
                CC,
                "-std=c89",
                "-O2",
                str(REPO_ROOT / "src" / "l80c" / "l80c.c"),
                "-o",
                str(cls.l80c),
            ],
            check=True,
            cwd=REPO_ROOT,
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.workspace, ignore_errors=True)

    def assemble(self, name, source, mode="/Z"):
        (self.workspace / (name + ".MAC")).write_text(source)
        return subprocess.run(
            [str(self.m80c), "=" + name + ".MAC", "/X", "/O", mode, "/L"],
            cwd=self.workspace,
            text=True,
            capture_output=True,
        )

    def linked_bytes(self, name):
        subprocess.run(
            [str(self.l80c), "/P:100," + name, "-o", name],
            cwd=self.workspace,
            text=True,
            capture_output=True,
            check=True,
        )
        return (self.workspace / (name + ".COM")).read_bytes()

    def assert_assembly_error(self, name, source, message):
        result = self.assemble(name, source)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        listing = (self.workspace / (name + ".PRN")).read_text()
        self.assertIn(message, listing)

    def test_largest_representable_rel_segments_are_accepted(self):
        for segment in ("CSEG", "DSEG"):
            with self.subTest(segment=segment):
                result = self.assemble(
                    "LIMIT", f"\t{segment}\n\tDS 65534\n\tDB 1\n\tORG 0\n\tDB 2\n\tEND\n"
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertTrue((self.workspace / "LIMIT.REL").is_file())

    def test_oversized_segments_are_rejected_before_rel_truncation(self):
        for segment in ("CSEG", "DSEG"):
            for body in ("DS 65536", "ORG 65536", "DS 65535\n\tDB 1",
                         "DS 65534\n\tDW 0", "DS 2147483647"):
                with self.subTest(segment=segment, body=body):
                    # A failed rebuild must not leave an older linkable object.
                    rel = self.workspace / "TOOBIG.REL"
                    rel.write_bytes(b"previous object")
                    result = self.assemble(
                        "TOOBIG", f"\t{segment}\n\t{body}\n\tEND\n"
                    )
                    self.assertNotEqual(result.returncode, 0)
                    expected = ("code" if segment == "CSEG" else "data")
                    self.assertIn(expected + " segment too large", result.stderr)
                    self.assertIn("65535-byte REL limit", result.stderr)
                    self.assertNotIn("No Fatal error(s)", result.stdout)
                    self.assertFalse(rel.exists())

    def test_initialized_storage_obeys_the_same_segment_limit(self):
        result = self.assemble(
            "BIGINIT", "\tCSEG\n\tDS 65536\n\tEND\n", mode="/Z /M"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("code segment too large", result.stderr)
        self.assertFalse((self.workspace / "BIGINIT.REL").exists())

    def test_undefined_symbol_is_an_error(self):
        self.assert_assembly_error(
            "UNDEF",
            "\tCSEG\nSTART:\n\tLD HL,MISSING\n\tEND START\n",
            "undefined symbol",
        )

    def test_duplicate_label_is_an_error(self):
        self.assert_assembly_error(
            "DUP",
            "\tCSEG\nFOO:\n\tNOP\nFOO:\n\tRET\n\tEND\n",
            "multiply defined symbol",
        )

    def test_out_of_range_relative_branch_is_an_error(self):
        self.assert_assembly_error(
            "JRANGE",
            "\tCSEG\nSTART:\n\tJR FAR\n\tDS 200\nFAR:\n\tRET\n\tEND START\n",
            "relative branch out of range",
        )

    def test_negative_origin_is_rejected_without_writing_memory(self):
        self.assert_assembly_error(
            "NEGOR",
            "\tCSEG\n\tORG -1\n\tDB 1\n\tEND\n",
            "negative origin",
        )

    def test_register_rotates_use_cb_encodings(self):
        result = self.assemble(
            "ROTATE",
            "\tCSEG\n\tRLC B\n\tRRC C\n\tEND\n",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.linked_bytes("ROTATE")[:4], bytes([0xCB, 0x00, 0xCB, 0x09]))

    def test_byte_register_absolute_loads_are_rejected(self):
        for reg in "BCDEHL":
            with self.subTest(register=reg):
                self.assert_assembly_error(
                    "BADLD" + reg,
                    "\tCSEG\n\tLD %s,(1234H)\n\tEND\n" % reg,
                    "unknown opcode",
                )

    def test_memory_loads_keep_valid_z80_encodings(self):
        result = self.assemble(
            "MEMLD",
            "\tCSEG\n\tLD DE,(1234H)\n\tLD A,(1234H)\n"
            "\tLD E,(HL)\n\tLD D,(IX+1)\n\tLD E,(IY+2)\n\tEND\n",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        expected = bytes.fromhex("ed 5b 34 12 3a 34 12 5e dd 56 01 fd 5e 02")
        self.assertEqual(self.linked_bytes("MEMLD")[:len(expected)], expected)

    def test_cpi_uses_selected_mnemonic_mode(self):
        intel = self.assemble("CPINT", "\tCSEG\n\tCPI 5\n\tEND\n", mode="/I")
        self.assertEqual(intel.returncode, 0, intel.stdout + intel.stderr)
        self.assertEqual(self.linked_bytes("CPINT")[:2], bytes([0xFE, 0x05]))

        z80 = self.assemble("CPZ80", "\tCSEG\n\tCPI\n\tEND\n")
        self.assertEqual(z80.returncode, 0, z80.stdout + z80.stderr)
        self.assertEqual(self.linked_bytes("CPZ80")[:2], bytes([0xED, 0xA1]))


if __name__ == "__main__":
    unittest.main()
