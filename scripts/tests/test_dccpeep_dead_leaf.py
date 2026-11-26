"""Unused leaf allocations may disappear only when the body cannot observe them."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless((ROOT / 'dccpeep').is_file(), 'built dccpeep required')
class DeadLeafAllocationTests(unittest.TestCase):
    def optimize(self, body, allocation=' dec sp\n dec sp\n'):
        source = (' cseg\n public _probe\n_probe:\n push ix\n ld ix,0\n add ix,sp\n' +
                  allocation + body + '\n ld sp,ix\n pop ix\n ret\n end\n')
        with tempfile.TemporaryDirectory(prefix='dcc-dead-leaf-') as directory:
            folder = Path(directory)
            (folder / 'IN.MAC').write_text(source)
            subprocess.run([str(ROOT / 'dccpeep'), str(folder / 'IN.MAC'), str(folder / 'OUT.MAC')],
                           check=True, capture_output=True, text=True, timeout=30)
            return (folder / 'OUT.MAC').read_text()

    def test_unused_compact_and_word_allocations_disappear(self):
        for allocation in (' dec sp\n dec sp\n', ' ld hl,-4\n add hl,sp\n ld sp,hl\n'):
            with self.subTest(allocation=allocation):
                output = self.optimize(' ld hl,(_value)\n inc hl\n ld (_value),hl', allocation)
                self.assertNotIn('ix', output)
                self.assertNotIn('dec sp', output)
                self.assertNotIn('add hl,sp', output)

    def test_calls_saved_values_and_stack_addresses_keep_the_allocation(self):
        for body in (' call _observe\n ld hl,0',
                     ' push hl\n ld hl,0\n ex (sp),hl\n pop de',
                     ' ld hl,0\n add hl,sp',
                     ' ld l,(ix-1)\n ld h,0',
                     ' push ix\n pop hl'):
            with self.subTest(body=body):
                self.assertIn('ix', self.optimize(body))

    def test_register_rotates_and_exchange_do_not_observe_the_frame(self):
        output = self.optimize(' ld hl,(_value)\n ld a,l\n sub 20\n ld a,h\n'
                               ' rla\n ccf\n rra\n sbc a,128\n ld hl,0\n rl l\n'
                               ' ld de,7\n ex de,hl\n add hl,de')
        self.assertNotIn('ix', output)
        self.assertIn('rl l', output)
        self.assertIn('ex de,hl', output)

    def test_live_allocation_result_remains_observable(self):
        output = self.optimize(' ld (_escaped),hl', ' ld hl,-4\n add hl,sp\n ld sp,hl\n')
        self.assertIn('ix', output)
        self.assertIn('add hl,sp', output)


if __name__ == '__main__':
    unittest.main()
