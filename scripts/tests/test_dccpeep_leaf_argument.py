"""Safety gates for caching an immutable argument across a framed leaf."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PEEP = ROOT / ("dccpeep.exe" if os.name == "nt" else "dccpeep")
FIXTURE = ROOT / "tests/dccpeep/leaf-word-argument.in.mac"

@unittest.skipUnless(PEEP.is_file(), "built dccpeep required")
class LeafArgumentTests(unittest.TestCase):
    def optimize(self, source):
        with tempfile.TemporaryDirectory(prefix="dcc-leaf-argument-") as folder:
            source_path = Path(folder) / "input.mac"
            output_path = Path(folder) / "output.mac"
            source_path.write_text(source, encoding="ascii")
            result = subprocess.run([str(PEEP), str(source_path), str(output_path)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return output_path.read_text(encoding="ascii")

    def test_reserves_an_unused_pair_and_is_idempotent(self):
        output = self.optimize(FIXTURE.read_text())
        self.assertIn("leaf_word_argument_cache", output)
        self.assertEqual(output, self.optimize(output))

    def test_volatile_source_parameter_has_a_cache_barrier(self):
        compiler = ROOT / ("dcc.exe" if os.name == "nt" else "dcc")
        if not compiler.is_file():
            self.skipTest("built dcc required")
        source = ("int leaf(volatile unsigned int x,unsigned int y,unsigned int z) {"
                  "int r=0; if(x<y)r++; if(x<z)r++; if(x<7)r++; if(x<17)r++; return r;}")
        with tempfile.TemporaryDirectory(prefix="dcc-volatile-argument-") as folder:
            path = Path(folder)
            (path / "source.c").write_text(source, encoding="ascii")
            result = subprocess.run([str(compiler), str(path / "source.c"),
                                     "-o", str(path / "source.mac")], cwd=ROOT,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            assembly = (path / "source.mac").read_text()
            self.assertIn("@dcc.volatile-parameter", assembly)
            self.assertNotIn("leaf_word_argument_cache", self.optimize(assembly))

    def test_declines_mutation_escape_calls_register_claims_and_other_entries(self):
        source = FIXTURE.read_text()
        cases = (
            ("call", "call _external\n"),
            ("store", "ld (ix+4),0\n"),
            ("frame alias", "push ix\npop hl\n"),
            ("register use", "ld bc,7\n"),
            ("register claim", ";@dcc.reg claim=bc scope=function sym=choose\n"),
            ("opaque instruction", "mystery\n"),
            ("volatile copy", ";@dcc.volatile-parameter\n"),
            ("alternate entry", "_alternate:\n"),
        )
        for name, instruction in cases:
            with self.subTest(name=name):
                changed = source.replace("ld hl,-2\n", instruction + "ld hl,-2\n", 1)
                self.assertNotIn("leaf_word_argument_cache", self.optimize(changed))
        unmarked = source.replace(";@dcc.lto begin _choose\n", "").replace(
            ";@dcc.lto end _choose\n", "")
        self.assertNotIn("leaf_word_argument_cache", self.optimize(unmarked))

if __name__ == "__main__":
    unittest.main()
