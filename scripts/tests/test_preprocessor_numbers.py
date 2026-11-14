"""Numeric preprocessing tokens must stay intact during macro expansion."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(
    (ROOT / "dccmake").is_file() and shutil.which("ntvcm"),
    "built dcc tools and ntvcm required",
)
class PreprocessorNumberTests(unittest.TestCase):
    def check_output(self, macros, body, expected):
        with tempfile.TemporaryDirectory(dir=ROOT / "build", prefix="pp-number-") as directory:
            work = Path(directory)
            source = work / "numbers.c"
            source.write_text(
                '#include <stdio.h>\n'
                '#define RAW(x) #x\n'
                '#define STR(x) RAW(x)\n'
                + macros + '\nint main(void) {\n' + body + '\nreturn 0;\n}\n',
                encoding="ascii",
            )
            for peep in ("false", "true"):
                with self.subTest(peep=peep):
                    build = work / peep
                    subprocess.run(
                        [str(ROOT / "dccmake"), f"dcc-input={source}",
                         "dcc-output=NUMBERS", f"dcc-build-dir={build}",
                         f"dcc-peep={peep}"],
                        cwd=ROOT, check=True, capture_output=True, text=True, timeout=30,
                    )
                    result = subprocess.run(
                        ["ntvcm", "-s:0", "NUMBERS.COM"], cwd=build,
                        check=True, capture_output=True, text=True, timeout=10,
                    )
                    self.assertEqual(result.stdout, expected)

    def test_replacement_parameters_are_not_suffixes(self):
        self.check_output(
            "#define BODY(f,u,l,L,U,F,ul) "
            "STR(9.0f 10u 11l 12L 13U .5F 40ul), STR(f u l L U F ul)",
            'printf("%s\\n%s\\n", BODY(a,b,c,d,h,i,j));',
            "9.0f 10u 11l 12L 13U .5F 40ul\na b c d h i j\n",
        )

    def test_argument_object_and_pasted_number_rescans(self):
        self.check_output(
            "#define f replaced\n#define u replaced\n#define ul replaced\n"
            "#define VALUE 9.0f\n#define CAT(a,b) a ## b",
            'printf("%s\\n%s\\n%s\\n", STR(9.0f 10u 40ul), '
            'STR(VALUE), STR(CAT(9.0,f)));',
            "9.0f 10u 40ul\n9.0f\n9.0f\n",
        )

    def test_pp_numbers_include_exponent_signs_and_identifier_characters(self):
        self.check_output(
            "#define e replaced\n#define E replaced\n#define p replaced\n"
            "#define P replaced\n#define f replaced\n#define u replaced\n"
            "#define _suffix replaced\n#define tail replaced",
            'printf("%s\\n", STR(1e+5 2E-3 0x1p+2 0X2P-4 .5f '
            '12_suffix 1.2.3 0xFE+tail 3+u 4-f));',
            "1e+5 2E-3 0x1p+2 0X2P-4 .5f 12_suffix 1.2.3 "
            "0xFE+tail 3+replaced 4-replaced\n",
        )


if __name__ == "__main__":
    unittest.main()
