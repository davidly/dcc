"""Code-generation checks; build dcc before running this module."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = """_Bool flag;
int hits;
int main(void) {
    while (flag) { hits++; if (hits > 4) break; }
    if (!flag) hits += 7;
    return hits;
}
"""


@unittest.skipUnless((ROOT / "dcc").is_file(), "built dcc required")
class BoolBranchCodegenTests(unittest.TestCase):
    def compile(self, source, options=(), candidate=None):
        with tempfile.TemporaryDirectory(prefix="dcc-bool-branch-") as directory:
            path = Path(directory)
            (path / "boolbr.c").write_text(source)
            env = os.environ.copy()
            if candidate:
                env.update(DCC_MIR_SELECT_CANDIDATE=candidate,
                           DCC_MIR_SELECT_FUNCTION="main")
            subprocess.run(
                [str(ROOT / "dcc"), *options, str(path / "boolbr.c"),
                 "-o", str(path / "boolbr.mac")],
                cwd=ROOT, env=env, check=True, capture_output=True, text=True, timeout=30,
            )
            return (path / "boolbr.mac").read_text()

    @staticmethod
    def instructions(assembly):
        return "\n".join(line.split(";", 1)[0].strip()
                         for line in assembly.splitlines()
                         if line.split(";", 1)[0].strip())

    def test_direct_and_negated_conditions(self):
        for options in [(), ("-gline",)]:
            with self.subTest(options=options):
                assembly = self.compile(SOURCE, options)
                code = self.instructions(assembly)
                self.assertRegex(code, r"ld a,\(_flag\)\nor a\njp z, L\d+")
                self.assertRegex(code, r"ld a,\(_flag\)\nor a\njp nz, L\d+")
                self.assertNotIn("ld hl,0", code)
                if options:
                    self.assertIn(";@dcc-line", assembly)

    def test_both_backends(self):
        for candidate in ["homed", "spilled-all"]:
            with self.subTest(candidate=candidate):
                code = self.instructions(self.compile(SOURCE, candidate=candidate))
                self.assertNotIn("ld hl,0", code)
                self.assertEqual(code.count("ld a,(_flag)\nor a\njp "), 2)

    def test_full_debug_keeps_normalization(self):
        code = self.instructions(self.compile(SOURCE, ("-g",)))
        self.assertIn("ld hl,0", code)

    def test_address_taken_local(self):
        source = ("extern void touch(_Bool *p); int hits; "
                  "int main(void) { _Bool local=0; touch(&local); "
                  "if(local) hits++; return hits; }")
        for candidate in ["homed", "spilled-all"]:
            with self.subTest(candidate=candidate):
                code = self.instructions(self.compile(source, candidate=candidate))
                self.assertRegex(code, r"ld a,\(ix-\d+\)\nor a\njp ")
                self.assertNotIn("ld hl,0", code.split("call _touch", 1)[1])

    def test_volatile_load_keeps_existing_path(self):
        code = self.instructions(self.compile(SOURCE.replace("_Bool flag;", "volatile _Bool flag;")))
        self.assertIn("ld hl,0", code)

    def test_value_consumer_keeps_normalization(self):
        code = self.instructions(self.compile("_Bool flag; int main(void) { return flag; }"))
        self.assertIn("ld hl,0", code)


if __name__ == "__main__":
    unittest.main()
