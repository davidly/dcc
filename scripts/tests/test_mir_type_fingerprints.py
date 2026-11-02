"""Exact schedules must not depend on unrelated aggregate declarations."""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
UNRELATED = """struct unused_first { long value; };
typedef struct { char bytes[13]; } unused_second;
struct unused_recursive { struct unused_recursive *next; int value; };
"""
APPS = ("bint", "adaint", "cint", "fint", "forint", "pint",
        "tchess", "texstrct")


@unittest.skipUnless((ROOT / "dcc").is_file(), "built dcc required")
class MirTypeFingerprintTests(unittest.TestCase):
    def compile(self, source, options=()):
        with tempfile.TemporaryDirectory(prefix="dcc-type-shape-") as directory:
            path = Path(directory)
            input_path = path / "probe.c"
            output_path = path / "probe.mac"
            input_path.write_text(source, encoding="ascii")
            env = os.environ.copy()
            env.update(DCC_MIR_REQUIRE_COMPLETE="1", DCC_MIR_REQUIRE_EMIT="1",
                       DCC_MIR_EXACT_SHAPE_REPORT="1")
            result = subprocess.run(
                [str(ROOT / "dcc"), "-stack", "512", "-I", str(ROOT),
                 *options, str(input_path), "-o", str(output_path)],
                cwd=ROOT, env=env, capture_output=True, text=True,
                check=True, timeout=30,
            )
            return output_path.read_text(encoding="ascii"), result.stderr

    @staticmethod
    def code(assembly):
        return "\n".join(line.split(";", 1)[0].strip()
                         for line in assembly.splitlines()
                         if line.split(";", 1)[0].strip())

    def test_unused_aggregates_preserve_all_affected_app_code(self):
        for app in APPS:
            source = (ROOT / "tests" / f"{app}.c").read_text(encoding="ascii")
            for options in ((), ("-fstack-check",), ("-gline",)):
                with self.subTest(app=app, options=options):
                    original, _ = self.compile(source, options)
                    shifted, _ = self.compile(UNRELATED + source, options)
                    self.assertEqual(self.code(original), self.code(shifted))

    def test_layout_and_signedness_changes_decline_struct_sort_kernel(self):
        source = (ROOT / "tests/texstrct.c").read_text(encoding="ascii")
        for options in ((), ("-fstack-check",), ("-gline",)):
            with self.subTest(options=options):
                original, _ = self.compile(source, options)
                self.assertIn(";@dcc.mir exact-kernel", original)
                for old, new in (("char name[8]", "char name[9]"),
                                 ("int  qty", "unsigned int qty")):
                    with self.subTest(mutation=new):
                        self.assertIn(old, source)
                        changed, _ = self.compile(
                            UNRELATED + source.replace(old, new), options)
                        self.assertNotIn(";@dcc.mir exact-kernel", changed)

    def test_distinct_aggregate_identities_remain_distinct(self):
        source = """struct A { int value; };
struct B { int value; };
int probe(struct A *a, struct B *b) {
    int total = a->value;
    b->value = total + 1;
    return a->value + b->value;
}
"""
        _, original = self.compile(source)
        _, shifted = self.compile(UNRELATED + source)
        _, collapsed = self.compile(source.replace("struct B *b", "struct A *b"))
        pattern = r"MIR exact-shape function=probe .*first=(\w+) second=(\w+)"
        original_hashes = set(re.findall(pattern, original))
        self.assertTrue(original_hashes)
        self.assertEqual(original_hashes, set(re.findall(pattern, shifted)))
        self.assertTrue(original_hashes.isdisjoint(re.findall(pattern, collapsed)))


if __name__ == "__main__":
    unittest.main()
