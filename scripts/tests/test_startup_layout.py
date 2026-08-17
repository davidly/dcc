import os
import re
import shutil
import subprocess
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = REPO_ROOT / "build"
RUNTIME = REPO_ROOT / "DCCRTL.MAC"
CC = shutil.which("clang") or shutil.which("gcc")


@unittest.skipUnless(CC, "host C compiler is required")
class StartupLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = BUILD_DIR / ("test-startup-layout-%d" % os.getpid())
        cls.workspace.mkdir(parents=True, exist_ok=True)
        cls.compiler = cls.workspace / "dcc"
        sources = sorted((REPO_ROOT / "src" / "dcc").glob("*.c"))
        subprocess.run(
            [
                CC,
                "-std=c11",
                "-O2",
                "-I",
                str(REPO_ROOT / "src" / "dcc"),
                *map(str, sources),
                "-o",
                str(cls.compiler),
            ],
            cwd=REPO_ROOT,
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.workspace, ignore_errors=True)

    def compile_source(self, name, source):
        source_path = self.workspace / (name + ".C")
        output_path = self.workspace / (name + ".MAC")
        source_path.write_text(source)
        subprocess.run(
            [str(self.compiler), str(source_path), "-o", str(output_path)],
            cwd=REPO_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        return output_path.read_text()

    @staticmethod
    def instruction_lines(text):
        lines = []
        for line in text.splitlines():
            line = line.split(";", 1)[0].strip()
            if line and not line.endswith(":"):
                lines.append(re.sub(r"\s+", " ", line))
        return lines

    def test_generated_near_64k_bss_wrap_is_rejected(self):
        assembly = self.compile_source(
            "WRAPBSS",
            "static unsigned char a[30000];\n"
            "static unsigned char b[30000];\n"
            "static unsigned char c[5020];\n"
            "int main(int argc, char **argv)\n"
            "{\n"
            "    return argc + (argv != 0);\n"
            "}\n",
        )
        bss_match = re.search(r"__bsse\s+equ\s+__bssb\+(\d+)", assembly)
        count_match = re.search(r"__bssn\s+equ\s+(\d+)", assembly)
        scratch_match = re.search(r"__hstart\s+equ\s+__bsse\+(\d+)", assembly)
        self.assertIsNotNone(bss_match)
        self.assertIsNotNone(count_match)
        self.assertIsNotNone(scratch_match)
        self.assertEqual(int(bss_match.group(1)), 0xFDFC)
        self.assertEqual(int(count_match.group(1)), 0xFDFC)
        self.assertEqual(int(scratch_match.group(1)), 260)

        bsse = 0x0100 + int(bss_match.group(1))
        hstart = (bsse + int(scratch_match.group(1))) & 0xFFFF
        self.assertEqual(bsse, 0xFEFC)
        self.assertEqual(hstart, 0)
        self.assertLess(hstart, bsse)

        lines = self.instruction_lines(RUNTIME.read_text())
        guard = [
            "exx",
            "ld hl,__hstart",
            "ld de,__bsse",
            "or a",
            "sbc hl,de",
            "exx",
            "jr c,mem_low",
        ]
        start = next(
            index
            for index in range(len(lines) - len(guard) + 1)
            if lines[index : index + len(guard)] == guard
        )
        self.assertLess(start, lines.index("ld de,__stack_size"))

    def test_scratch_wrap_boundary(self):
        self.assertEqual((0xFEFB + 260) & 0xFFFF, 0xFFFF)
        self.assertGreaterEqual((0xFEFB + 260) & 0xFFFF, 0xFEFB)
        self.assertEqual((0xFEFC + 260) & 0xFFFF, 0)
        self.assertLess((0xFEFC + 260) & 0xFFFF, 0xFEFC)

    def test_main_without_arguments_needs_no_scratch(self):
        assembly = self.compile_source(
            "NOARGV",
            "int main(void)\n"
            "{\n"
            "    return 0;\n"
            "}\n",
        )
        self.assertRegex(assembly, r"__hstart\s+equ\s+__bsse(?:\s|$)")
        self.assertNotRegex(assembly, r"__hstart\s+equ\s+__bsse\+")


if __name__ == "__main__":
    unittest.main()
