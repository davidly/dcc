import os
import shutil
import subprocess
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
RTL_STRIP_SOURCE = REPO_ROOT / "src/dccrtlstrip/dccrtlstrip.c"
APP_STRIP_SOURCE = REPO_ROOT / "src/dccrtlstrip/dcc_app_strip.c"


class DccAppStripTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = REPO_ROOT / "build" / (
            "test-app-strip-%d" % os.getpid()
        )
        cls.workspace.mkdir(parents=True, exist_ok=True)
        cls.tool = cls.workspace / "dccrtlstrip"
        subprocess.run(
            [
                "cc",
                "-std=c89",
                "-O2",
                str(RTL_STRIP_SOURCE),
                str(APP_STRIP_SOURCE),
                "-o",
                str(cls.tool),
            ],
            cwd=REPO_ROOT,
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.workspace, ignore_errors=True)

    def write_module(self, name, text):
        path = self.workspace / name
        path.write_text(text)
        return path

    def strip(self, *paths, roots=()):
        command = [str(self.tool), "--strip-apps"]
        for root in roots:
            command += ["-k", root]
        command += [str(path) for path in paths]
        return subprocess.run(
            command,
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

    def test_cross_module_function_object_and_string_reachability(self):
        main = self.write_module(
            "MAIN.MAC",
            """\
\tcseg
;@dcc.lto begin _main
\tpublic _main
_main:
\tcall _live
\tld hl,S0
\tret
;@dcc.lto end _main
;@dcc.lto begin __mrun
\tpublic __mrun
__mrun:
\tjp _main
;@dcc.lto end __mrun
;@dcc.lto begin S0
S0:
\tdb 79,75,0
;@dcc.lto end S0
;@dcc.lto begin S1
S1:
\tdb 68,69,65,68,0
;@dcc.lto end S1
\tend
""",
        )
        module = self.write_module(
            "MOD.MAC",
            """\
\tcseg
;@dcc.lto begin _live
\tpublic _live
_live:
\tcall _helper
\tld hl,(_livev)
\tret
;@dcc.lto end _live
;@dcc.lto begin _dead
\tpublic _dead
_dead:
\tld hl,(_deadv)
\tret
;@dcc.lto end _dead
;@dcc.lto begin _helper
_helper:
\tinc hl
\tret
;@dcc.lto end _helper
;@dcc.lto begin _livev
\tpublic _livev
_livev:
\tdw 7
;@dcc.lto end _livev
;@dcc.lto begin _deadv
\tpublic _deadv
_deadv:
\tdw 99
;@dcc.lto end _deadv
\tend
""",
        )

        result = self.strip(main, module)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        main_text = main.read_text()
        module_text = module.read_text()
        self.assertIn("_main:", main_text)
        self.assertIn("__mrun:", main_text)
        self.assertIn("S0:", main_text)
        self.assertNotIn("S1:", main_text)
        self.assertIn("_live:", module_text)
        self.assertIn("_helper:", module_text)
        self.assertIn("_livev:", module_text)
        self.assertNotIn("_dead:", module_text)
        self.assertNotIn("_deadv:", module_text)

    def test_address_taken_reference_keeps_callback(self):
        module = self.write_module(
            "CALLBACK.MAC",
            """\
\tcseg
;@dcc.lto begin __mrun
\tpublic __mrun
__mrun:
\tld hl,_table
\tret
;@dcc.lto end __mrun
;@dcc.lto begin _table
\tpublic _table
_table:
\tdw _callback
;@dcc.lto end _table
;@dcc.lto begin _callback
\tpublic _callback
_callback:
\tld hl,42
\tret
;@dcc.lto end _callback
\tend
""",
        )

        result = self.strip(module)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("_callback:", module.read_text())

    def test_internal_label_reference_keeps_owning_block(self):
        module = self.write_module(
            "THREAD.MAC",
            """\
\tcseg
;@dcc.lto begin _main
\tpublic _main
_main:
L15:
\tld hl,0
\tret
;@dcc.lto end _main
;@dcc.lto begin __mrun
\tpublic __mrun
__mrun:
\tjp L15
;@dcc.lto end __mrun
\tend
""",
        )

        result = self.strip(module)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("_main:", module.read_text())

    def test_secondary_public_label_keeps_owning_block_across_modules(self):
        main = self.write_module(
            "PUBMAIN.MAC",
            """\
\tcseg
;@dcc.lto begin __mrun
\tpublic __mrun
__mrun:
\tcall _alias
\tret
;@dcc.lto end __mrun
\tend
""",
        )
        module = self.write_module(
            "PUBMOD.MAC",
            """\
\tcseg
;@dcc.lto begin _primary
\tpublic _primary,_alias
_primary:
_alias:
\tld hl,1
\tret
;@dcc.lto end _primary
\tend
""",
        )

        result = self.strip(main, module)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("_primary:", module.read_text())
        self.assertIn("_alias:", module.read_text())

    def test_module_local_names_do_not_cross_keep(self):
        first = self.write_module(
            "FIRST.MAC",
            """\
\tcseg
;@dcc.lto begin __mrun
\tpublic __mrun
__mrun:
\tld hl,S0
\tret
;@dcc.lto end __mrun
;@dcc.lto begin S0
S0:
\tdb 1
;@dcc.lto end S0
\tend
""",
        )
        second = self.write_module(
            "SECOND.MAC",
            """\
\tcseg
;@dcc.lto begin _dead
\tpublic _dead
_dead:
\tld hl,S0
\tret
;@dcc.lto end _dead
;@dcc.lto begin S0
S0:
\tdb 2
;@dcc.lto end S0
\tend
""",
        )

        result = self.strip(first, second)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("S0:", first.read_text())
        self.assertNotIn("S0:", second.read_text())

    def test_explicit_root_keeps_exported_entry(self):
        module = self.write_module(
            "ROOT.MAC",
            """\
\tcseg
;@dcc.lto begin _entry
\tpublic _entry
_entry:
\tret
;@dcc.lto end _entry
;@dcc.lto begin _dead
\tpublic _dead
_dead:
\tret
;@dcc.lto end _dead
\tend
""",
        )

        result = self.strip(module, roots=("_entry",))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("_entry:", module.read_text())
        self.assertNotIn("_dead:", module.read_text())

    def test_reachable_use_keeps_extern_declared_only_in_dead_block(self):
        module = self.write_module(
            "EXTERN.MAC",
            """\
\tcseg
;@dcc.lto begin _dead
\tpublic _dead
_dead:
\textrn __helper
\tcall __helper
\tret
;@dcc.lto end _dead
;@dcc.lto begin __mrun
\tpublic __mrun
__mrun:
\tcall __helper
\tret
;@dcc.lto end __mrun
\tend
""",
        )

        result = self.strip(module)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        text = module.read_text()
        self.assertNotIn("_dead:", text)
        self.assertIn("extrn __helper", text)
        self.assertIn("call __helper", text)

    def test_unmarked_extern_declaration_does_not_root_dead_block(self):
        main = self.write_module(
            "EXTMAIN.MAC",
            """\
\tcseg
;@dcc.lto begin __mrun
\tpublic __mrun
__mrun:
\tret
;@dcc.lto end __mrun
\textrn _dead
\tend
""",
        )
        module = self.write_module(
            "EXTMOD.MAC",
            """\
\tcseg
;@dcc.lto begin _dead
\tpublic _dead
_dead:
\tret
;@dcc.lto end _dead
\tend
""",
        )

        result = self.strip(main, module)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("extrn _dead", main.read_text())
        self.assertNotIn("_dead:", module.read_text())

    def test_malformed_marker_does_not_rewrite_any_module(self):
        good = self.write_module(
            "GOOD.MAC",
            """\
;@dcc.lto begin __mrun
__mrun:
\tret
;@dcc.lto end __mrun
""",
        )
        bad = self.write_module(
            "BAD.MAC",
            """\
;@dcc.lto begin _bad
_bad:
\tret
""",
        )
        original_good = good.read_text()
        original_bad = bad.read_text()

        result = self.strip(good, bad)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(good.read_text(), original_good)
        self.assertEqual(bad.read_text(), original_bad)


if __name__ == "__main__":
    unittest.main()
