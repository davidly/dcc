"""Regression controls for oracle seeds 212413, 228464, 206417 and 206531."""
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless((ROOT / 'dccpeep').is_file(), 'built dccpeep required')
class FrameBankPeepholeTests(unittest.TestCase):
    def optimize(self, source, mode='-Ot'):
        with tempfile.TemporaryDirectory(prefix='dcc-frame-banks-') as folder:
            folder = Path(folder)
            src, out = folder / 'IN.MAC', folder / 'OUT.MAC'
            src.write_text(source)
            subprocess.run([str(ROOT / 'dccpeep'), mode, str(src), str(out)],
                           check=True, capture_output=True, text=True, timeout=30)
            return out.read_text()

    def test_bc_hoisting_stays_enabled_without_bank_switches(self):
        source = '''_probe:
L1:
 ld l,(ix+4)
 ld h,(ix+5)
 ld a,(hl)
 or a
 jp nz,L1
 ret
 end
'''
        for mode in ('-Ot', '-Os'):
            with self.subTest(mode=mode):
                self.assertIn('hoist_index_ptr_to_bc', self.optimize(source, mode))

    def test_bc_hoisting_declines_loads_in_the_alternate_bank(self):
        source = '''_probe:
L1:
 exx
 ld l,(ix+4)
 ld h,(ix+5)
 push hl
 exx
 pop de
 ld a,(de)
 or a
 jp nz,L1
 ret
 end
'''
        for mode in ('-Ot', '-Os'):
            for exchange in ('exx', 'exx ; register-bank switch'):
                with self.subTest(mode=mode, exchange=exchange):
                    self.assertNotIn('hoist_index_ptr_to_bc',
                                     self.optimize(source.replace('exx', exchange), mode))

    def test_iy_reads_keep_pending_ix_stores_live(self):
        for bias, slot in ((-141, -41), (-130, -3), (0, 4)):
            for label in ('', 'Lread:\n'):
                with self.subTest(bias=bias, slot=slot, label=label):
                    source = '''_probe:
 push ix
 pop iy
 ld bc,%d
 add iy,bc
 %sld (ix%+d),17
 ld a,(iy%+d)
 ld (ix%+d),29
 ret
 end
''' % (bias, label, slot, slot - bias, slot)
                    output = self.optimize(source)
                    self.assertIn('ld (ix%+d),17' % slot, output)
                    self.assertNotIn('ld (ix%+d),29' % slot, output)

    def test_non_aliasing_ix_reads_still_allow_dead_store_removal(self):
        source = '''_probe:
 ld (ix-41),17
 ld a,(ix-40)
 ld (ix-41),29
 ret
 end
'''
        output = self.optimize(source)
        self.assertNotIn('ld (ix-41)', output)
        self.assertIn('ld a,(ix-40)', output)


@unittest.skipUnless((ROOT / 'dcc').is_file(), 'built dcc required')
class FloatShortCircuitCodegenTests(unittest.TestCase):
    def test_skipped_division_folds_to_the_same_code_as_nonzero_control(self):
        with tempfile.TemporaryDirectory(prefix='dcc-float-logical-') as folder:
            folder = Path(folder)
            for options in ((), ('-gline',)):
                for operator, comparison, result in (('||', '<', 1), ('&&', '>', 0)):
                    outputs = []
                    for denominator in ('0.0f', '1.0f'):
                        source, output = folder / 'logic.c', folder / 'logic.mac'
                        source.write_text('unsigned int result(void) { return '
                                          '(1.0f %s 2.0f) %s (1.0f / %s); }\n'
                                          % (comparison, operator, denominator))
                        subprocess.run([str(ROOT / 'dcc'), *options, str(source),
                                        '-o', str(output)], cwd=ROOT, check=True,
                                       capture_output=True, text=True, timeout=30)
                        code = '\n'.join(line.split(';', 1)[0].strip()
                                         for line in output.read_text().splitlines()
                                         if line.split(';', 1)[0].strip())
                        outputs.append(code)
                    with self.subTest(options=options, operator=operator):
                        self.assertEqual(outputs[0], outputs[1])
                        self.assertIn('ld hl,%d\nret' % result, outputs[0])
                        self.assertNotIn('__fdf', outputs[0])


if __name__ == '__main__':
    unittest.main()
