"""Safety gates for the bounded recursive-byte equality rewrite."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/dccpeep/recursive-byte-range.closed.in.mac"


@unittest.skipUnless((ROOT / "dccpeep").is_file(), "built dccpeep required")
class RecursiveByteRangeTests(unittest.TestCase):
    def optimize(self, text, closed=True):
        with tempfile.TemporaryDirectory(prefix="dcc-cp8-") as directory:
            folder = Path(directory)
            source, output = folder / "IN.MAC", folder / "OUT.MAC"
            source.write_text(text, encoding="ascii")
            command = [str(ROOT / "dccpeep")]
            if closed:
                command.append("-closed-program")
            result = subprocess.run(
                [*command, str(source), str(output)],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return output.read_text(encoding="ascii")

    def test_proof_is_structural_and_requires_closed_callers(self):
        text = FIXTURE.read_text(encoding="ascii").replace("_walk", "_counter")
        self.assertIn("cp8_to_bit3", self.optimize(text))
        self.assertIn("cp8_to_bit3", self.optimize(text.replace("ld c,0", "ld c,8")))
        self.assertNotIn("cp8_to_bit3", self.optimize(text, closed=False))

    def test_declines_unproven_roots_edges_and_frame_aliases(self):
        text = FIXTURE.read_text(encoding="ascii")
        cases = (
            ("initial nine", "ld c,0", "ld c,9"),
            ("step two", "inc a\nld c,a", "add a,2\nld c,a"),
            ("bypass terminal check", "cp 4", "cp 9"),
            ("address taken", "end\n", "dw _walk\nend\n"),
            ("parameter changed", "cp 4", "ld (ix+6),0\ncp 4"),
            ("escaped frame", "cp 4", "push ix\npop hl\ncp 4"),
            ("unbounded second caller", "end\n",
             "public _other\n_other:\nld c,9\npush bc\n"
             "ld hl,0\npush hl\ncall _walk\npop bc\npop bc\nret\nend\n"),
        )
        for name, old, new in cases:
            with self.subTest(name=name):
                self.assertIn(old, text)
                self.assertNotIn("cp8_to_bit3", self.optimize(text.replace(old, new)))


if __name__ == "__main__":
    unittest.main()
