import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
REQUIRED_TOOLS = [
    REPO_ROOT / "dcc",
    REPO_ROOT / "dccmake",
    REPO_ROOT / "dccpeep",
    REPO_ROOT / "dccrtlstrip",
    REPO_ROOT / "m80c",
    REPO_ROOT / "l80c",
]


@unittest.skipUnless(
    all(path.is_file() for path in REQUIRED_TOOLS),
    "built DCC tools are required",
)
class LineDebugTests(unittest.TestCase):
    def test_maximum_identifier_lto_markers_match(self):
        with tempfile.TemporaryDirectory(prefix="dcc-lto-name-") as directory:
            root = Path(directory)
            name = "f" * 63
            source = root / "longname.c"
            output = root / "LONGNAME.MAC"
            source.write_text(
                "int %s(void) { return 1; }\n"
                "int main(void) { return %s() != 1; }\n" % (name, name),
                encoding="ascii",
            )

            subprocess.run(
                [
                    str(REPO_ROOT / "dcc"),
                    "-I",
                    str(REPO_ROOT),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            )
            text = output.read_text(encoding="ascii")
            starts = [
                line for line in text.splitlines()
                if line.startswith(";@dcc.lto begin ")
            ]
            ends = [
                line for line in text.splitlines()
                if line.startswith(";@dcc.lto end ")
            ]
            self.assertEqual(
                [line[len(";@dcc.lto begin "):] for line in starts],
                [line[len(";@dcc.lto end "):] for line in ends],
            )

    def test_multimodule_line_debug_matches_release_code(self):
        with tempfile.TemporaryDirectory(prefix="dcc-line-debug-") as directory:
            root = Path(directory)
            (root / "main.c").write_text(
                "int helper(int value);\n"
                "int main(void)\n"
                "{\n"
                "    return helper(4) != 5;\n"
                "}\n",
                encoding="ascii",
            )
            (root / "module.c").write_text(
                "int helper(int value)\n"
                "{\n"
                "    return value + 1;\n"
                "}\n"
                "int unused_helper(int value)\n"
                "{\n"
                "    return value * 7;\n"
                "}\n",
                encoding="ascii",
            )
            (root / "dccmake.txt").write_text(
                "dcc-input=main.c,module.c\n"
                "dcc-output=LINEDEB\n"
                "dcc-debug=lines\n"
                f"dcc-runtime={REPO_ROOT / 'DCCRTL.MAC'}\n"
                f"dcc-include-directory={REPO_ROOT}\n"
                f"dcc-tool={REPO_ROOT / 'dcc'}\n"
                f"dccpeep-tool={REPO_ROOT / 'dccpeep'}\n"
                f"dccrtlstrip-tool={REPO_ROOT / 'dccrtlstrip'}\n"
                f"m80-command={REPO_ROOT / 'm80c'}\n"
                f"l80-command={REPO_ROOT / 'l80c'}\n",
                encoding="ascii",
            )

            subprocess.run(
                [str(REPO_ROOT / "dccmake"), "dcc-debug=false"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            )
            release = (root / "build" / "LINEDEB.COM").read_bytes()

            subprocess.run(
                [str(REPO_ROOT / "dccmake"), "-g"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            )
            optimized = (root / "build" / "LINEDEB.COM").read_bytes()
            metadata = (root / "build" / "LINEDEB.DBG").read_text(
                encoding="ascii"
            )

            self.assertEqual(optimized, release)
            self.assertTrue(metadata.startswith("DCCDBG 2\n"))
            self.assertIn('"main.c"', metadata)
            self.assertIn('"module.c"', metadata)
            self.assertNotIn("unused_helper", metadata)
            self.assertEqual(
                metadata.count("function-begin "),
                metadata.count("function-end "),
            )
            self.assertGreater(metadata.count("function-begin "), 0)
            self.assertIn("\nvariable ", metadata)
            self.assertIn("\nlocation ", metadata)
            self.assertRegex(metadata, r"\nlocation [0-9A-F]{4} .* (?:frame|hl|de|bc|iy|const|out) -?[0-9]+\n")


if __name__ == "__main__":
    unittest.main()