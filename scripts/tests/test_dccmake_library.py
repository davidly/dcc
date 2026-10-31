import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ("dcc", "dccmake", "dccpeep", "dccrtlstrip", "m80c", "l80c")
LIB_LINKER = Path(os.environ.get("DCC_L80C", str(ROOT / "l80c")))


@unittest.skipUnless(all((ROOT / tool).is_file() for tool in TOOLS),
                     "built DCC tools are required")
class DccmakeLibraryTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="dccmake library ", dir=ROOT / "build")
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)
        (self.folder / "LIB.C").write_text(
            "static volatile unsigned char bss[2048];\n"
            "unsigned int onload(unsigned int p) {\n"
            " bss[0] = (unsigned char)p;\n"
            " bss[2047] = (unsigned char)(p >> 8);\n"
            " return bss[0] + bss[2047];\n"
            "}\n"
            "unsigned int call(unsigned int p) { return p + 7; }\n",
            encoding="ascii")

    def configure(self, **overrides):
        settings = {
            "dcc-input": "LIB.C",
            "dcc-output": "SPREADS",
            "dcc-output-format": "lib",
            "dcc-exports": "auto",
            "dcc-library-api": "auto",
            "dcc-library-host-dir": "generated",
            "dcc-stack-bytes": "512",
            "dcc-debug": "false",
            "dcc-runtime": str(ROOT / "DCCRTL.MAC"),
            "dcc-include-directory": str(ROOT),
            "dcc-tool": str(ROOT / "dcc"),
            "dccpeep-tool": str(ROOT / "dccpeep"),
            "dccrtlstrip-tool": str(ROOT / "dccrtlstrip"),
            "m80c-tool": str(ROOT / "m80c"),
            "l80c-tool": str(LIB_LINKER),
            "dcc-use-emulated-m80": "false",
            "dcc-use-emulated-l80": "false",
        }
        settings.update(overrides)
        (self.folder / "dccmake.txt").write_text(
            "".join(f"{key}={value}\n" for key, value in settings.items()),
            encoding="ascii")

    def build(self, *arguments):
        return subprocess.run(
            [str(ROOT / "dccmake"), *arguments], cwd=self.folder,
            capture_output=True, text=True,
            env={**os.environ, "DCC_ARGS": ""})

    def require_library_linker(self):
        if not LIB_LINKER.is_file():
            self.skipTest("a built native l80c linker is required")

    def test_invalid_library_settings_fail_before_compilation(self):
        cases = [
            ({"dcc-output-format": "archive"}, "invalid dcc-output-format"),
            ({"dcc-output-format": "com"}, "dcc-exports requires"),
            ({"dcc-exports": ""}, "library output requires dcc-exports=auto"),
            ({"dcc-exports": "_LOAD=_ONLOAD,_CALL"}, "invalid dcc-exports"),
            ({"dcc-exports": "CALL"}, "invalid dcc-exports"),
            ({"dcc-library-api": ""}, "requires dcc-library-api and dcc-library-host-dir"),
            ({"dcc-library-host-dir": ""}, "requires dcc-library-api and dcc-library-host-dir"),
            ({"dcc-use-emulated-l80": "true"}, "requires a native"),
        ]
        cases += [({"dcc-stack-bytes": str(size)}, "256..2048")
                  for size in (0, 255, 257, 2049, 2304)]
        for settings, message in cases:
            with self.subTest(settings=settings):
                self.configure(**settings)
                result = self.build()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.assertFalse((self.folder / "build").exists())

    def test_default_com_pipeline_still_builds(self):
        (self.folder / "LIB.C").write_text(
            "int main(void) { return 0; }\n", encoding="ascii")
        self.configure(**{"dcc-exports": "", "l80c-tool": str(ROOT / "l80c")})
        config = self.folder / "dccmake.txt"
        config.write_text(config.read_text().replace("dcc-output-format=lib\n", ""))
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.folder / "build/SPREADS.COM").is_file())
        self.assertFalse((self.folder / "build/SPREADS.LIB").exists())
        self.assertIn("start:", (self.folder / "build/RTLMIN.MAC").read_text())

    def test_library_header_bss_exports_and_runtime(self):
        self.require_library_linker()
        self.configure()
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        library = (self.folder / "build/SPREADS.LIB").read_bytes()
        word = lambda offset: int.from_bytes(library[offset:offset + 2], "little")
        self.assertEqual(library[:8], b"B1LB\x01\x01\x00\x01")
        self.assertEqual((word(8), word(12), word(16), word(18)),
                         (0x100, 2, 512, 50))
        self.assertEqual(
            word(24), 0x100 + word(10) + 2048,
            (self.folder / "build/SPREADS.MAC").read_text()[-2500:])
        self.assertNotEqual(word(20), 0xFFFF)
        self.assertNotEqual(word(22), 0xFFFF)
        self.assertEqual(library[26:34], b"ONLOAD\0\0")
        self.assertEqual(library[38:46], b"CALL\0\0\0\0")
        image = library[word(18):word(18) + word(10)]
        for entry, signature in ((26, b"u16(u16)"), (38, b"u16(u16)")):
            self.assertLess(word(entry + 8), word(10))
            offset = word(entry + 10)
            self.assertEqual(image[offset:image.index(0, offset)], signature)
        self.assertEqual(word(14), 0)
        runtime = (self.folder / "build/RTLMIN.MAC").read_text()
        self.assertNotIn("\nstart:", runtime)
        self.assertNotIn("jp      0000h", runtime)
        self.assertNotIn("\tend\tstart", runtime)
        self.assertTrue((self.folder / "build/SPREADS.SYM").is_file())

    def test_library_runtime_memory_snapshot_resolves_stack_symbol(self):
        self.require_library_linker()
        (self.folder / "LIB.C").write_text(
            "#include <stdlib.h>\n"
            "unsigned int onload(void) {\n"
            " meminfo_t info;\n"
            " meminfo(&info);\n"
            " return info.stack_size;\n"
            "}\n")
        self.configure()
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        runtime = (self.folder / "build/RTLMIN.MAC").read_text()
        self.assertIn("extrn __stack_size", runtime)
        self.assertIn("_meminfo:", runtime)
        self.assertNotIn("_malloc:", runtime)
        self.assertTrue((self.folder / "build/SPREADS.LIB").is_file())

    def test_multimodule_library_and_stack_override(self):
        self.require_library_linker()
        source = self.folder / "LIB.C"
        source.write_text(
            "unsigned int helper(unsigned int v);\n"
            + source.read_text().replace("return p + 7", "return helper(p)"))
        (self.folder / "HELP.C").write_text(
            "unsigned int helper(unsigned int v) { return v + 7; }\n",
            encoding="ascii")
        self.configure(**{"dcc-input": "LIB.C,HELP.C"})
        result = self.build("dcc-stack-bytes=1024")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        library = (self.folder / "build/SPREADS.LIB").read_bytes()
        self.assertEqual(int.from_bytes(library[16:18], "little"), 1024)
        self.assertIn("'--lib'", result.stdout)
        self.assertIn("HELP", result.stdout)

    def test_main_is_not_a_library_entry(self):
        self.require_library_linker()
        (self.folder / "LIB.C").write_text(
            "int main(void) { return 0; }\n", encoding="ascii")
        self.configure()
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("transient-program startup or main", result.stderr)
        self.assertFalse((self.folder / "build/SPREADS.LIB").exists())

    def test_unsupported_runtime_is_reported(self):
        runtime = self.folder / "CUSTOM.MAC"
        runtime.write_text("cseg\nend\n", encoding="ascii")
        self.configure(**{"dcc-runtime": str(runtime)})
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unsupported library runtime layout", result.stderr)

    def test_compiler_raised_stack_is_not_misreported_in_header(self):
        (self.folder / "LIB.C").write_text(
            "unsigned int call(unsigned int p) {\n"
            " unsigned char local[800];\n"
            " local[0] = p; local[799] = 7;\n"
            " return local[0] + local[799];\n"
            "}\n", encoding="ascii")
        self.configure()
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("increase dcc-stack-bytes", result.stderr)
        self.assertFalse((self.folder / "build/SPREADS.LIB").exists())


if __name__ == "__main__":
    unittest.main()
