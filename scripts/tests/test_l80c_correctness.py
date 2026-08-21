import os
import shutil
import subprocess
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = REPO_ROOT / "build"
CC = shutil.which("clang") or shutil.which("gcc")


@unittest.skipUnless(CC, "host C compiler is required")
class L80cCorrectnessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = BUILD_DIR / ("test-l80c-correctness-%d" % os.getpid())
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

    def assemble(self, name, source):
        (self.workspace / (name + ".MAC")).write_text(source)
        result = subprocess.run(
            [str(self.m80c), "=" + name + ".MAC", "/X", "/O", "/Z"],
            cwd=self.workspace,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def link(self, command, *args):
        return subprocess.run(
            [str(self.l80c), command] + list(args),
            cwd=self.workspace,
            text=True,
            capture_output=True,
        )

    @staticmethod
    def malformed_rel(cseg_size, code_lc, link_value=False):
        bits = []

        def put(value, width):
            bits.extend((value >> shift) & 1 for shift in range(width - 1, -1, -1))

        def value(value_type, number):
            put(value_type, 2)
            put(number & 0xFF, 8)
            put((number >> 8) & 0xFF, 8)

        def name(text):
            put(len(text), 3)
            for character in text:
                put(ord(character), 8)

        def special(code, value_type=None, number=0, relname=None):
            put(1, 1)
            put(0, 2)
            put(code, 4)
            if value_type is not None:
                value(value_type, number)
            if relname is not None:
                name(relname)

        special(2, relname="BAD")
        special(10, 0, 0)
        special(13, 1, cseg_size)
        special(11, 1, code_lc)
        if link_value:
            put(1, 1)
            value(1, 0)
        else:
            put(0, 1)
            put(0xAA, 8)

        while len(bits) % 8:
            bits.append(0)
        return bytes(
            sum(bits[index + bit] << (7 - bit) for bit in range(8))
            for index in range(0, len(bits), 8)
        )

    def test_standard_origin_links_relocated_code(self):
        self.assemble(
            "BASIC",
            "\tCSEG\n\tPUBLIC START\nSTART:\n\tLD HL,START\n\tRET\n\tEND START\n",
        )
        result = self.link("/P:100,BASIC", "-o", "BASIC")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(
            (self.workspace / "BASIC.COM").read_bytes()[:4],
            bytes([0x21, 0x00, 0x01, 0xC9]),
        )

    def test_nonstandard_origin_is_rejected(self):
        self.assemble("ORIGIN", "\tCSEG\n\tRET\n\tEND\n")
        result = self.link("/P:200,ORIGIN")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CP/M .COM programs load at 0100", result.stderr)

    def test_invalid_origin_is_rejected(self):
        self.assemble("BADORG", "\tCSEG\n\tRET\n\tEND\n")
        result = self.link("/P:nothex,BADORG")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid origin", result.stderr)

    def test_rel_data_cannot_exceed_declared_segment(self):
        (self.workspace / "BAD.REL").write_bytes(
            self.malformed_rel(1, 1)
        )
        result = self.link("/P:100,BAD")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("code range 1..2 out of bounds", result.stderr)

    def test_rel_link_value_must_fit_declared_segment(self):
        (self.workspace / "BADLINK.REL").write_bytes(
            self.malformed_rel(1, 0, link_value=True)
        )
        result = self.link("/P:100,BADLINK")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("link range 0..2 out of bounds", result.stderr)

    def test_rel_location_counter_must_fit_declared_segment(self):
        (self.workspace / "BADLC.REL").write_bytes(
            self.malformed_rel(1, 2)
        )
        result = self.link("/P:100,BADLC")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("code LC range 2..2 out of bounds", result.stderr)

    def test_com_suffix_is_not_duplicated(self):
        self.assemble("SUFFIX", "\tCSEG\n\tRET\n\tEND\n")
        result = self.link("/P:100,SUFFIX", "-o", "NAMED.COM")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.workspace / "NAMED.COM").is_file())
        self.assertTrue((self.workspace / "NAMED.SYM").is_file())
        self.assertFalse((self.workspace / "NAMED.COM.COM").exists())
        self.assertFalse((self.workspace / "NAMED.COM.SYM").exists())

        result = self.link("/P:100,SUFFIX,MARKED.COM/N/E/Y")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.workspace / "MARKED.COM").is_file())
        self.assertTrue((self.workspace / "MARKED.SYM").is_file())
        self.assertFalse((self.workspace / "MARKED.COM.COM").exists())


if __name__ == "__main__":
    unittest.main()
