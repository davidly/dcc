"""Preserve the distinct findings and controls from seeds 70001-120000."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
try:
    import clang_oracle_gen as integer
    import clang_oracle_float_gen as floating
finally:
    sys.path.pop(0)


class CampaignSteeringTests(unittest.TestCase):
    def test_original_integer_seeds_are_steered(self):
        # The original seeds predate the new optional generator constructs.
        original_off = frozenset({'union', 'comma', 'pdiff', 'varargs'})
        for seed in (70940, 86893, 91438, 108843):
            with self.subTest(seed=seed):
                program = integer.generate(seed, off=original_off | frozenset(integer.KNOWN_BUGS))
                self.assertFalse(integer.known_shapes(program))

    def test_unsigned_initializer_positive_constant_still_needs_steering(self):
        generator = integer.Gen(0, off={'initinterpret'})
        source = integer.mk_const(88, integer.U32)
        init, assignment = generator.initialiser(None, integer.I32, source, 'local')
        self.assertIs(assignment, source)
        self.assertIs(integer.ty(init), integer.I32)
        self.assertFalse(integer.initializer_reinterprets(
            integer.mk_const(88, integer.I32), integer.I32))

    def test_float_seeds_are_detected_before_and_absent_after_steering(self):
        for seed, feature in ((77632, 'fconstdivzero'), (82776, 'fconstdivzero'),
                              (94893, 'fupdatepair')):
            with self.subTest(seed=seed):
                original = floating.generate(seed)
                original.off = frozenset(floating.KNOWN_BUGS)
                self.assertIn(feature, floating.known_shapes(original))
                steered = floating.generate(seed, off=frozenset(floating.KNOWN_BUGS))
                self.assertFalse(floating.known_shapes(steered))

    def test_nonzero_and_variable_float_denominators_remain_eligible(self):
        numerator = floating.E('const', a=0.001, t=floating.F32)
        for denominator in (floating.E('const', a=1.0, t=floating.F32),
                            floating.E('var', a=floating.Decl('x', floating.F32, 'local'))):
            expression = floating.E('bin', [numerator, denominator], '/')
            self.assertFalse(floating.float_const_div_zero(expression))

    def test_scalar_float_updates_are_not_steered(self):
        operands = [floating.E('incdec', [floating.E('var', a=floating.Decl(
            name, floating.F32, 'global'))], ('++', False)) for name in ('x', 'y')]
        self.assertFalse(floating.float_update_pair(floating.E('bin', operands, '>')))
