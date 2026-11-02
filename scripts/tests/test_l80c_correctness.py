import os
import shutil
import subprocess
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = REPO_ROOT / "build"
CC = shutil.which("clang") or shutil.which("gcc")

LIBRARY_SOURCE = (
    "\tCSEG\n\tPUBLIC _ENTRY\n_ENTRY:\n\tRET\n"
    "\tPUBLIC _SIG\n_SIG:\n\tDB 'u16()',0\n"
    "\tPUBLIC _BAD\n_BAD:\n\tDB 255,0\n"
    "\tPUBLIC _EMPTY\n_EMPTY:\n\tDB 0\n"
    "\tPUBLIC _TAIL\n_TAIL:\n\tDB 'A'\n"
    "\tPUBLIC _PAST\n_PAST:\n\tEND\n"
)


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

    def test_high_origin_inserts_start_jump_and_padding(self):
        self.assemble(
            "HIGH",
            "\tCSEG\n\tNOP\nSTART:\n\tRET\n\tEND START\n",
        )
        result = self.link("/P:200,HIGH", "-o", "HIGH")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        image = (self.workspace / "HIGH.COM").read_bytes()
        self.assertEqual(image[:3], bytes([0xC3, 0x01, 0x02]))
        self.assertEqual(image[3:0x100], bytes(0xFD))
        self.assertEqual(image[0x100:0x102], bytes([0x00, 0xC9]))

    def test_zero_origin_uses_copy_bootstrap(self):
        self.assemble(
            "ZERO",
            "\tCSEG\nSTART:\n\tLD C,0\n\tJP 5\n\tEND START\n",
        )
        result = self.link("/P:0,ZERO", "-o", "ZERO")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        image = (self.workspace / "ZERO.COM").read_bytes()
        self.assertEqual(image[:3], bytes([0xC3, 0x08, 0x01]))
        self.assertEqual(image[3:8], bytes([0x0E, 0x00, 0xC3, 0x05, 0x00]))
        self.assertEqual(
            image[8:30],
            bytes(
                [
                    0x21,
                    0x03,
                    0x01,
                    0x11,
                    0x00,
                    0x00,
                    0x01,
                    0x05,
                    0x00,
                    0x7E,
                    0x12,
                    0x23,
                    0x13,
                    0x0B,
                    0x78,
                    0xB1,
                    0xC2,
                    0x11,
                    0x01,
                    0xC3,
                    0x00,
                    0x00,
                ]
            ),
        )

    def test_origin_inside_entry_slot_suppresses_jump(self):
        self.assemble("ENTRY101", "\tCSEG\n\tRET\n\tEND\n")
        result = self.link("/P:101,ENTRY101", "-o", "ENTRY101")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        image = (self.workspace / "ENTRY101.COM").read_bytes()
        self.assertEqual(image[:2], bytes([0x00, 0xC9]))

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

    def test_library_stack_sizes_match_loader_limits(self):
        self.assemble("LIBSTACK", LIBRARY_SOURCE)
        for size in range(256, 2049, 256):
            with self.subTest(valid_stack=size):
                result = self.link(
                    "/P:100,LIBSTACK", "--lib", "--export", "ENTRY=_entry", "--signature", "ENTRY=_sig",
                    "--stack-bytes", str(size), "-o", "STACKOK",
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                image = (self.workspace / "STACKOK.LIB").read_bytes()
                self.assertEqual(int.from_bytes(image[16:18], "little"), size)
        for index, size in enumerate((0, 1, 255, 257, 2049, 2304, 16384, 65535)):
            with self.subTest(invalid_stack=size):
                output = f"BADSTK{index}"
                result = self.link(
                    "/P:100,LIBSTACK", "--lib", "--export", "ENTRY=_entry", "--signature", "ENTRY=_sig",
                    "--stack-bytes", str(size), "-o", output,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("256..2048", result.stderr)
                self.assertFalse((self.workspace / (output + ".LIB")).exists())

    def test_library_image_must_end_below_default_stack(self):
        # Without a DCC runtime the heap base is the image end; the default
        # 2 KiB stack starts at C000h, so the image may extend to BFFFh.
        for size in (0xBEFE, 0xBEFF, 0xBF00):
            with self.subTest(image_size=size):
                self.assemble(
                    "LIBSIZE",
                    "\tCSEG\n\tPUBLIC _ENTRY\n_ENTRY:\n\tRET\n"
                    "\tPUBLIC _SIG\n_SIG:\n\tDB 'u16()',0\n"
                    f"\tDS {size - 7}\n\tEND\n",
                )
                output = f"SIZE{size:X}"
                result = self.link(
                    "/P:100,LIBSIZE", "--lib", "--export", "ENTRY=_entry", "--signature", "ENTRY=_sig",
                    "-o", output,
                )
                path = self.workspace / (output + ".LIB")
                if size < 0xBF00:
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    image = path.read_bytes()
                    self.assertEqual(int.from_bytes(image[10:12], "little"), size)
                else:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("overlaps the configured stack", result.stderr)
                    self.assertFalse(path.exists())

    def test_invalid_library_metadata_does_not_touch_outputs(self):
        self.assemble("LIBCHECK", LIBRARY_SOURCE)
        base = ("/P:100,LIBCHECK", "--lib", "--export", "ENTRY=_entry")
        signed = ("--signature", "ENTRY=_sig")
        arguments = base + signed
        cases = (
            ((), "library export ENTRY has no --signature"),
            (signed + ("--export", "MORE=_entry"), "library export MORE has no --signature"),
            (signed + ("--export", "BAD=_none", "--signature", "BAD=_sig"),
             "missing or out-of-image library export"),
            (signed + ("--export", "BAD=_past", "--signature", "BAD=_sig"),
             "missing or out-of-image library export"),
            (signed + ("--signature", "OTHER=_sig"), "signature has no matching export"),
            (("--signature", "ENTRY=_none"), "missing or out-of-image signature"),
            (("--signature", "ENTRY=_past"), "missing or out-of-image signature"),
            (("--signature", "ENTRY=_bad"), "non-ASCII library signature"),
            (("--signature", "ENTRY=_empty"), "invalid or unterminated signature"),
            (("--signature", "ENTRY=_tail"), "invalid or unterminated signature"),
        )
        for options, message in cases:
            with self.subTest(options=options):
                good = self.link(*arguments, "-o", "KEEP")
                self.assertEqual(good.returncode, 0, good.stdout + good.stderr)
                previous = {
                    suffix: (self.workspace / ("KEEP." + suffix)).read_bytes()
                    for suffix in ("LIB", "SYM")
                }
                failed = self.link(*base, *options, "-o", "KEEP")
                self.assertNotEqual(failed.returncode, 0)
                self.assertIn(message, failed.stderr)
                for suffix, content in previous.items():
                    self.assertEqual(
                        (self.workspace / ("KEEP." + suffix)).read_bytes(), content,
                    )

                failed = self.link(*base, *options, "-o", "NEWBAD")
                self.assertNotEqual(failed.returncode, 0)
                self.assertIn(message, failed.stderr)
                for suffix in ("LIB", "SYM"):
                    self.assertFalse((self.workspace / ("NEWBAD." + suffix)).exists())

    def test_library_header_and_signed_export_entries(self):
        self.assemble("LIBHDR", LIBRARY_SOURCE)
        result = self.link(
            "/P:100,LIBHDR", "--lib", "--export", "ENTRY=_entry",
            "--signature", "ENTRY=_sig", "--export", "SECOND=_entry",
            "--signature", "SECOND=_sig", "-o", "HEADER",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        data = (self.workspace / "HEADER.LIB").read_bytes()
        word = lambda offset: int.from_bytes(data[offset:offset + 2], "little")
        self.assertEqual(data[:8], b"B1LB\x01\x01\x00\x01")
        self.assertEqual(word(8), 0x100)
        self.assertEqual(word(12), 2)
        self.assertEqual(word(14), 0)
        self.assertEqual(word(16), 2048)
        self.assertEqual(word(18), 26 + 12 * 2)
        self.assertEqual((word(20), word(22)), (0xFFFF, 0xFFFF))
        self.assertEqual(word(24), 0x100 + word(10))
        image = data[word(18):word(18) + word(10)]
        for index, name in enumerate((b"ENTRY", b"SECOND")):
            entry = 26 + 12 * index
            self.assertEqual(data[entry:entry + 8], name.ljust(8, b"\0"))
            self.assertEqual(word(entry + 8), 0)
            signature = word(entry + 10)
            self.assertEqual(image[signature:image.index(0, signature)], b"u16()")
        self.assertEqual(len(data) % 128, 0)

    def test_signature_requires_library_mode(self):
        self.assemble("NOLIB", LIBRARY_SOURCE)
        result = self.link("/P:100,NOLIB", "--signature", "ENTRY=_sig", "-o", "NOLIB")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--signature requires --lib", result.stderr)

    def test_library_suffix_is_not_duplicated(self):
        self.assemble("LIBSUF", LIBRARY_SOURCE)
        for name, suffix in (("NAMED", "LIB"), ("LOWER", "lib"), ("MIXED", "LiB")):
            with self.subTest(suffix=suffix):
                result = self.link(
                    "/P:100,LIBSUF", "--lib", "--export", "ENTRY=_entry", "--signature", "ENTRY=_sig",
                    "-o", name + "." + suffix,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertTrue((self.workspace / (name + ".LIB")).is_file())
                self.assertTrue((self.workspace / (name + ".SYM")).is_file())
                for extra in ("LIB", "SYM"):
                    self.assertFalse(
                        (self.workspace / (name + "." + suffix + "." + extra)).exists(),
                    )

        result = self.link(
            "/P:100,LIBSUF,LIBMARK.LIB/N/E/Y", "--lib", "--export", "ENTRY=_entry", "--signature", "ENTRY=_sig",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.workspace / "LIBMARK.LIB").is_file())
        self.assertTrue((self.workspace / "LIBMARK.SYM").is_file())
        self.assertFalse((self.workspace / "LIBMARK.LIB.LIB").exists())

        result = self.link(
            "/P:100,LIBSUF", "--lib", "--export", "ENTRY=_entry", "--signature", "ENTRY=_sig", "-o", ".LIB",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("empty output name", result.stderr)


if __name__ == "__main__":
    unittest.main()
