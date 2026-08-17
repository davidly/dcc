import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
RUNTIME = REPO_ROOT / "DCCRTL.MAC"
TPA_BASE = 0x0100
FCB_BYTES = 36
TRAMPOLINE_BYTES = 44
STACK_GAP_BYTES = 16
RESERVED_BYTES = FCB_BYTES + TRAMPOLINE_BYTES + STACK_GAP_BYTES


def loader_layout(fbase, records, r2=0):
    if r2 or fbase < RESERVED_BYTES:
        return None
    fcb_base = fbase - FCB_BYTES - TRAMPOLINE_BYTES
    stack_start = fcb_base - STACK_GAP_BYTES
    if stack_start < TPA_BASE:
        return None
    load_end = TPA_BASE + records * 128
    if load_end > stack_start:
        return None
    return {
        "load": (TPA_BASE, load_end),
        "stack": (stack_start, fcb_base),
        "fcb": (fcb_base, fcb_base + FCB_BYTES),
        "trampoline": (fcb_base + FCB_BYTES, fbase),
    }


def helper_model(fbase, records, r2=0):
    if r2:
        return None
    fcb_base = fbase - (FCB_BYTES + TRAMPOLINE_BYTES)
    if fcb_base < 0:
        return None
    available = fcb_base - (TPA_BASE + STACK_GAP_BYTES)
    if available < 0:
        return None
    if records > available // 128:
        return None
    return fcb_base


def trampoline_model(approved, statuses):
    successful = 0
    attempts = 0
    while successful < approved:
        if attempts >= len(statuses):
            return "failure", successful, attempts
        status = statuses[attempts]
        attempts += 1
        if status:
            return "failure", successful, attempts
        successful += 1
    return "jump", successful, attempts


class ExecLoaderSafetyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = RUNTIME.read_text()

    def block(self, start, end):
        match = re.search(
            rf"(?ms)^{re.escape(start)}:\s*$.*?(?=^{re.escape(end)}:)",
            self.runtime,
        )
        self.assertIsNotNone(match)
        return match.group(0)

    def test_helper_matches_exact_16_bit_range_model(self):
        for fbase in range(0x10000):
            available = fbase - RESERVED_BYTES - TPA_BASE
            capacity = available // 128 if available >= 0 else -1
            candidates = {0, 1, max(0, capacity), max(0, capacity + 1), 0x1FF, 0x200}
            for records in candidates:
                expected = loader_layout(fbase, records)
                actual = helper_model(fbase, records)
                self.assertEqual(
                    None if expected is None else expected["fcb"][0],
                    actual,
                    (fbase, records),
                )

    def test_dma_record_rounding_stops_before_every_reserved_range(self):
        layout = loader_layout(0xF000, 477)
        self.assertIsNotNone(layout)
        self.assertEqual(layout["load"], (0x0100, 0xEF80))
        self.assertEqual(layout["stack"], (0xEFA0, 0xEFB0))
        self.assertEqual(layout["fcb"], (0xEFB0, 0xEFD4))
        self.assertEqual(layout["trampoline"], (0xEFD4, 0xF000))
        for record in range(477):
            dma = (TPA_BASE + record * 128, TPA_BASE + (record + 1) * 128)
            for name in ("stack", "fcb", "trampoline"):
                reserved = layout[name]
                self.assertTrue(dma[1] <= reserved[0] or dma[0] >= reserved[1])

        self.assertIsNone(loader_layout(0xF000, 478))
        unsafe_dma = (0xEF80, 0xF000)
        self.assertLess(unsafe_dma[0], layout["stack"][1])
        self.assertGreater(unsafe_dma[1], layout["stack"][0])

    def test_maximum_16_bit_boundary_does_not_wrap(self):
        self.assertEqual(helper_model(0xFFFF, 509), 0xFFAF)
        self.assertIsNone(helper_model(0xFFFF, 510))
        self.assertIsNone(helper_model(0xF000, 0, 1))
        self.assertIsNone(helper_model(0x015F, 0))
        self.assertEqual(helper_model(0x0160, 0), 0x0110)

    def test_preflight_occurs_before_stack_switch(self):
        main = self.block("__xmain", "__xfit")
        instructions = [
            "ld      c,35",
            "call    5",
            "call    __xfit",
            "jr      c,xm_big",
            "ld      (__xtemp+XTCNTO),de",
            "ld      sp,hl",
        ]
        positions = []
        cursor = 0
        for instruction in instructions:
            cursor = main.index(instruction, cursor)
            positions.append(cursor)
            cursor += len(instruction)
        self.assertEqual(positions, sorted(positions))
        self.assertIn("ld      e,(__xfcbb+33)", main)
        self.assertIn("ld      d,(__xfcbb+34)", main)
        self.assertIn("ld      a,(__xfcbb+35)", main)
        self.assertIn("ld      hl,EINVAL", main)
        self.assertIn("ld      hl,ENOENT", main)
        self.assertIn("ld      hl,EFBIG", main)
        self.assertIn("ld      (_errno),hl", main)

    def test_fit_helper_uses_record_capacity_not_wrapping_byte_end(self):
        fit = self.block("__xfit", "__xacom")
        self.assertIn("ld      bc,XTBLK", fit)
        self.assertIn("ld      bc,0110h", fit)
        self.assertIn("ld      b,7", fit)
        self.assertIn("srl     h", fit)
        self.assertIn("rr      l", fit)
        self.assertIn("sbc     hl,de", fit)
        self.assertNotIn("add     hl,hl", fit)

    def test_exact_count_trampoline_stops_or_fails_without_partial_jump(self):
        template = self.block("__xtemp", "__xfit")
        self.assertIn("XTCOD=44", self.runtime)
        self.assertIn(
            "db      001h,000h,000h  ; ld bc,0      approved records",
            template,
        )
        self.assertIn(
            "db      011h,000h,000h  ; ld de,0      high FCB; patch 21,22",
            template,
        )
        self.assertIn(
            "db      0c2h,000h,000h  ; jp nz,0000h early EOF/error",
            template,
        )
        self.assertIn("db      020h,0dfh       ; jr nz,loop  (-33)", template)
        self.assertNotIn("jp nz,0100h", template)

        self.assertEqual(trampoline_model(3, [0, 0, 0]), ("jump", 3, 3))
        self.assertEqual(
            trampoline_model(3, [0, 1, 0]), ("failure", 1, 2)
        )
        self.assertEqual(
            trampoline_model(2, [0, 0, 0]), ("jump", 2, 2)
        )
        self.assertEqual(trampoline_model(0, [0]), ("jump", 0, 0))

    def test_raw_tail_is_staged_before_common_exec_side_effects(self):
        exec_block = self.block("_exec", "_execv")
        self.assertLess(
            exec_block.index("call    __xcpct"),
            exec_block.index("jp      __xmain"),
        )
        self.assertIn("ld      hl,__xctbf", exec_block)
        self.assertIn("ld      b,127", exec_block)
        self.assertIn("ld      (__xctln),a", exec_block)

        main_start = self.runtime.index("__xmain:")
        scratch = self.runtime.index("__xctbf:")
        fit_start = self.runtime.index("__xfit:")
        self.assertLess(main_start, scratch)
        self.assertLess(scratch, fit_start)
        main = self.runtime[main_start:fit_start]
        self.assertNotIn("__xctls", main)
        self.assertNotIn("__xpath", main)

    def test_tail_copy_and_fcb_parse_are_length_bounded_to_127(self):
        setup = self.block("__xsctl", "__xclfb")
        self.assertIn("ld      a,(__xctln)", setup)
        self.assertIn("ld      hl,__xctbf", setup)
        self.assertIn("ld      (0080h),a", setup)
        self.assertIn("cp      127", setup)
        self.assertIn("jr      z,xsc_parse", setup)
        self.assertNotIn("ld      hl,0081h", setup)

    def test_path_and_default_fcb_copies_are_bounded(self):
        path = self.block("__xacom", "__xwcp")
        word = self.block("__xwcp", "_signal")
        self.assertIn("ld      b,8", path)
        self.assertIn("ld      b,3", path)
        self.assertIn(
            "db      '<','>','.',',',';',':','=','?','*','[',']','%','|','(',')','/','\\'",
            path,
        )
        self.assertIn("ld      c,15", word)
        self.assertIn("ld      a,b", word)
        self.assertIn("cp      ' '+1", word)
        self.assertIn("xwc_skip:", word)
        self.assertIn("__xpbuf:        ds      16", self.runtime)

    def test_startup_and_default_fcb_use_same_delimiter_rule(self):
        startup = self.block("__build_argv", "__argc")
        skip = self.block("__xsksp", "__xwcp")
        word = self.block("__xwcp", "_signal")
        self.assertIn("cp      ' '+1", startup)
        self.assertIn("cp      ' '+1", skip)
        self.assertIn("cp      ' '+1", word)
        self.assertNotIn("cp      0dh", word)


if __name__ == "__main__":
    unittest.main()
