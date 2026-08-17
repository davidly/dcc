import os
import shutil
import subprocess
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "src/dccrtlstrip/dccrtlstrip.c"
RUNTIME = REPO_ROOT / "DCCRTL.MAC"


class DccRtlStripBlockTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = REPO_ROOT / "build" / (
            "test-strip-blocks-%d" % os.getpid()
        )
        cls.workspace.mkdir(parents=True, exist_ok=True)
        cls.tool = cls.workspace / "dccrtlstrip"
        subprocess.run(
            ["cc", "-O2", str(SOURCE), "-o", str(cls.tool)],
            cwd=REPO_ROOT,
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.workspace, ignore_errors=True)

    def strip_roots(self, *symbols):
        app = self.workspace / "APP.MAC"
        output = self.workspace / "RTLMIN.MAC"
        lines = []
        for symbol in symbols:
            lines.append("        extrn   %s\n" % symbol)
            lines.append("        call    %s\n" % symbol)
        lines.append("        end\n")
        app.write_text("".join(lines))
        subprocess.run(
            [
                str(self.tool),
                "-r",
                str(RUNTIME),
                "-o",
                str(output),
                str(app),
            ],
            cwd=REPO_ROOT,
            check=True,
        )
        return output.read_text()

    def strip_root(self, symbol):
        return self.strip_roots(symbol)

    def test_getchar_keeps_pushback_without_file_input(self):
        rtl = self.strip_root("__gchr")
        self.assertIn("__ugvalid:", rtl)
        self.assertIn("__ugchars:", rtl)
        self.assertIn("__ugget:", rtl)
        self.assertNotIn("__fgetc:", rtl)
        self.assertNotIn("___getc_ch:", rtl)
        self.assertNotIn("_read:", rtl)

    def test_fgetc_keeps_private_byte_and_shared_pushback(self):
        rtl = self.strip_root("__fgetc")
        self.assertIn("___getc_ch:", rtl)
        self.assertIn("__ugvalid:", rtl)
        self.assertIn("__ugchars:", rtl)
        self.assertIn("__ugget:", rtl)
        self.assertIn("_read:", rtl)

    def test_multibyte_counters_stay_in_owning_blocks(self):
        rtl = self.strip_root("_mblen")
        self.assertNotIn("__mbs_n:", rtl)
        self.assertNotIn("__wcs_n:", rtl)
        self.assertNotIn("_exec:", rtl)

        rtl = self.strip_root("_mbstowcs")
        self.assertIn("__mbs_n:", rtl)
        self.assertNotIn("__wcs_n:", rtl)
        self.assertNotIn("_wcstombs:", rtl)

        rtl = self.strip_root("_wcstombs")
        self.assertIn("__wcs_n:", rtl)
        self.assertNotIn("__mbs_n:", rtl)
        self.assertNotIn("_mbstowcs:", rtl)

    def test_exec_does_not_keep_multibyte_counters(self):
        rtl = self.strip_root("_exec")
        self.assertIn("_exec:", rtl)
        self.assertNotIn("__mbs_n:", rtl)
        self.assertNotIn("__wcs_n:", rtl)

    def test_tmpnam_does_not_keep_tmpfile_constants(self):
        rtl = self.strip_root("_tmpnam")
        self.assertIn("_tmpnam:", rtl)
        self.assertNotIn("NTMP     equ    4", rtl)
        self.assertNotIn("TN_NSZ   equ    16", rtl)
        self.assertNotIn("_tmpfile:", rtl)

    def test_tmpfile_keeps_its_constants_without_fclose(self):
        rtl = self.strip_root("_tmpfile")
        self.assertIn("NTMP     equ    4", rtl)
        self.assertIn("TN_NSZ   equ    16", rtl)
        self.assertNotIn("_fclose:", rtl)

    def test_sinh_and_cosh_do_not_keep_tanh(self):
        for symbol in ("_sinhf", "_coshf"):
            rtl = self.strip_root(symbol)
            self.assertIn("__hypd:", rtl)
            self.assertIn("__hyp_xlo:", rtl)
            self.assertNotIn("_tanhf:", rtl)

    def test_gmtime_and_localtime_keep_only_calendar_core(self):
        for symbol in ("_gmtime", "__ltim"):
            rtl = self.strip_root(symbol)
            self.assertIn("_gmtime:", rtl)
            self.assertIn("__cal_core:", rtl)
            self.assertIn("CDZ0004:", rtl)
            self.assertNotIn("_mktime:", rtl)
            self.assertNotIn("_difftime:", rtl)
            self.assertNotIn("__asc_data:", rtl)

    def test_asctime_keeps_only_formatting_data(self):
        rtl = self.strip_root("_asctime")
        self.assertIn("__asc_data:", rtl)
        self.assertIn("CDS0:", rtl)
        self.assertNotIn("__cal_core:", rtl)
        self.assertNotIn("_mktime:", rtl)
        self.assertNotIn("_difftime:", rtl)

    def test_mktime_keeps_core_without_other_public_time_functions(self):
        rtl = self.strip_root("_mktime")
        self.assertIn("__cal_core:", rtl)
        self.assertNotIn("_difftime:", rtl)
        self.assertNotIn("__asc_data:", rtl)

    def test_accepted_fastcall_entries_exclude_general_prologues(self):
        pairs = (
            ("__scf", "__scpy"),
            ("__icf", "__sicm"),
            ("__mhf", "__mchr"),
            ("__cmpf", "__mcmp"),
            ("__mcf", "__mcpy"),
        )
        for fast, general in pairs:
            rtl = self.strip_root(fast)
            self.assertIn(fast + ":", rtl)
            self.assertNotIn(general + ":", rtl)

            rtl = self.strip_root(general)
            self.assertIn(general + ":", rtl)
            self.assertIn(fast + ":", rtl)

    def test_rejected_fastcall_reorders_remain_unchanged(self):
        pairs = (
            ("__chf", "__schr"),
            ("__rcf", "__srch"),
            ("__ssf", "__sstr"),
            ("__msf", "__mset"),
        )
        for fast, general in pairs:
            rtl = self.strip_root(fast)
            self.assertIn(fast + ":", rtl)
            self.assertIn(general + ":", rtl)

    def test_fused_divmod_excludes_cache_but_cached_entries_keep_it(self):
        rtl = self.strip_root("__udivmod")
        self.assertIn("__udivmod:", rtl)
        self.assertNotIn("__dmcx:", rtl)
        self.assertNotIn("__dmval:", rtl)

        for symbol in ("__divu", "__modu"):
            rtl = self.strip_root(symbol)
            self.assertIn("__dmcx:", rtl)
            self.assertIn("__dmval:", rtl)

    def test_q2u_is_direct_alias_and_qsort_isolated(self):
        rtl = self.strip_root("__q2u")
        self.assertIn("__q2u:\nD16U:", rtl)
        self.assertNotIn("call    D16U             ; fixed divider quotient", rtl)

        rtl = self.strip_root("_qsort")
        self.assertIn("_qsort:", rtl)
        self.assertNotIn("_bsearch:", rtl)
        self.assertNotIn("_abs:", rtl)

    def test_combined_roots_keep_only_required_shared_classes(self):
        rtl = self.strip_roots(
            "__gchr",
            "_mbstowcs",
            "_exec",
            "_tmpnam",
            "_sinhf",
            "_gmtime",
            "__scf",
            "__udivmod",
            "_qsort",
        )
        for marker in (
            "__ugget:",
            "__mbs_n:",
            "_exec:",
            "_tmpnam:",
            "__hypd:",
            "__cal_core:",
            "__scf:",
            "__udivmod:",
            "_qsort:",
        ):
            self.assertIn(marker, rtl)
        for marker in (
            "__fgetc:",
            "___getc_ch:",
            "__wcs_n:",
            "_tmpfile:",
            "_tanhf:",
            "_difftime:",
            "_mktime:",
            "__scpy:",
            "__dmcx:",
            "_bsearch:",
        ):
            self.assertNotIn(marker, rtl)


if __name__ == "__main__":
    unittest.main()
