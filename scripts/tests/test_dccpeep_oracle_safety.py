"""Negative controls for the frame-alias and live-return oracle fixes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tests/dccpeep/small-add-join-return.in.mac'


@unittest.skipUnless((ROOT / 'dccpeep').is_file(), 'built dccpeep required')
class OraclePeepholeSafetyTests(unittest.TestCase):
    def optimize(self, source, closed=False):
        with tempfile.TemporaryDirectory(prefix='dcc-oracle-peep-', dir=ROOT / 'build') as folder:
            folder = Path(folder)
            input_path, output_path = folder / 'IN.MAC', folder / 'OUT.MAC'
            input_path.write_text(source)
            subprocess.run([str(ROOT / 'dccpeep'), *(['-closed-program'] if closed else []),
                            str(input_path), str(output_path)], check=True,
                           capture_output=True, text=True, timeout=30)
            return output_path.read_text()

    def caller(self, body, escaped=False, public=False):
        source = FIXTURE.read_text().replace(' end\n', '')
        if public:
            source = source.replace('; static function join_value', 'public _join_value')
        return source + ('dw _join_value\n' if escaped else '') + (
            '; static function caller\n_caller:\n call _join_value\n' + body + '\n ret\n end\n')

    def test_return_and_partial_byte_overwrite_stay_live_through_join(self):
        source = FIXTURE.read_text()
        for replacement in ('L2:\n', 'L2:\n ld h,0\n', 'L2:\n ld l,0\n'):
            with self.subTest(replacement=replacement):
                output = self.optimize(source.replace('L2:\n', replacement))
                self.assertNotIn('small_const_incr_carry_skip', output)

    def test_only_proven_unused_private_or_closed_returns_can_be_discarded(self):
        for body in (' ld hl,0', ' call _stack_args_only\n ld hl,0'):
            with self.subTest(body=body):
                self.assertIn('small_const_incr_carry_skip', self.optimize(self.caller(body)))
                self.assertNotIn('small_const_incr_carry_skip', self.optimize(
                    self.caller(body, public=True)))
                self.assertIn('small_const_incr_carry_skip', self.optimize(
                    self.caller(body, public=True), closed=True))

    def test_observed_or_escaped_function_result_remains_live(self):
        for source in (self.caller(' ld a,l\n ld hl,0'),
                       self.caller(' ld hl,0', escaped=True),
                       self.caller(' call __register_args\n ld hl,0')):
            with self.subTest(source=source):
                self.assertNotIn('small_const_incr_carry_skip', self.optimize(source, closed=True))

    def test_balanced_save_is_only_ignored_when_the_saved_value_is_unused(self):
        save = ' push hl\n ld hl,9\n ld a,l\n pop hl\n'
        for after, optimized in ((' ld hl,0', True), (' ld a,l\n ld hl,0', False)):
            output = self.optimize(self.caller(save + after))
            self.assertEqual('small_const_incr_carry_skip' in output, optimized)

    def test_conditional_returns_do_not_hide_a_later_use(self):
        output = self.optimize(self.caller(' ret z\n ld a,l\n ld hl,0'))
        self.assertNotIn('small_const_incr_carry_skip', output)

    def test_cyclic_join_proof_declines_without_unbounded_recursion(self):
        source = FIXTURE.read_text().replace('L2:\n', 'L2:\n jp L2\n')
        self.assertNotIn('small_const_incr_carry_skip', self.optimize(source))

    def test_spill_cache_declines_unknown_frame_addresses(self):
        source = (ROOT / 'tests/dccpeep/exx-spill-union-alias.in.mac').read_text()
        for address in (' ld de,-8\n add hl,de', ' add hl,bc',
                        ' ld hl,0\n add hl,sp'):
            with self.subTest(address=address):
                changed = source.replace(' ld de,-8\n add hl,de', address)
                self.assertNotIn('ix_spill_exx', self.optimize(changed))
