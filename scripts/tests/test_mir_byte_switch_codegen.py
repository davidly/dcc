"""Small dense byte selectors should use bounded tables before dccpeep."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless((ROOT / 'dcc').is_file(), 'built dcc required')
class ByteSwitchCodegenTests(unittest.TestCase):
    def test_eight_sparse_byte_cases_use_a_bounded_table(self):
        arms = ''.join('case %d: return side(%d);' % pair for pair in
                       [(1,3), (2,7), (4,11), (7,13), (9,17), (12,19), (14,23), (15,29)])
        source = ('struct Op { unsigned char op; int arg; }; extern int side(int);'
                  'int probe(struct Op *p) { switch(p->op) {' + arms +
                  'default: return side(-1); }}')
        for options, escaped in [(o, e) for o in ([], ['-fstack-check'], ['-gline']) for e in (False, True)]:
            with self.subTest(options=options, escaped=escaped), tempfile.TemporaryDirectory() as directory:
                path = Path(directory)
                probe = source
                if escaped:
                    probe = probe.replace('int probe(struct Op *p) { switch(p->op)',
                        'extern void escape(struct Op **); int probe(struct Op *p) {'
                        ' struct Op *q = p; escape(&q); switch(q->op)')
                (path / 'probe.c').write_text(probe)
                env = dict(os.environ, DCC_MIR_SELECT_CANDIDATE='spilled-all',
                           DCC_MIR_SELECT_FUNCTION='probe', DCC_MIR_REQUIRE_EMIT='1')
                result = subprocess.run([str(ROOT/'dcc'), '-c', '-I', str(ROOT), *options,
                                         str(path/'probe.c'), '-o', str(path/'probe.mac')],
                                        env=env, capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                asm = (path/'probe.mac').read_text()
                self.assertIn('jp (hl)', asm)
                if escaped:
                    self.assertRegex(asm, r'ld a,\(hl\)\n\s*sub 1\n\s*jp c, L\d+\n\s*cp 15\n\s*jp nc, L\d+\n\s*add a,a')
                self.assertEqual(len(re.findall(r'^\s*dw L\d+', asm, re.M)), 15)
                self.assertRegex(asm, r'sub 1\n\s*jp c, L\d+\n\s*cp 15\n\s*jp nc, L\d+')
