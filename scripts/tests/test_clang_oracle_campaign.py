"""Preserve the distinct findings and controls from seeds 70001-120000."""
from pathlib import Path
import runpy
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
try:
    import clang_oracle_gen as integer
    import clang_oracle_float_gen as floating
    import clang_oracle_frontend_gen as frontend
finally:
    sys.path.pop(0)


class CampaignSteeringTests(unittest.TestCase):
    def test_original_integer_seeds_keep_optional_steering(self):
        # The original seeds predate the new optional generator constructs.
        original_off = frozenset({'union', 'comma', 'pdiff', 'varargs'})
        for seed in (70940, 86893, 91438, 108843):
            with self.subTest(seed=seed):
                program = integer.generate(seed, off=original_off | {
                    'initinterpret', 'bfunsignedshift'})
                self.assertFalse(integer.known_shapes(program))

    def test_unsigned_initializer_positive_constant_keeps_optional_steering(self):
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
                original.off = frozenset({feature})
                self.assertIn(feature, floating.known_shapes(original))
                steered = floating.generate(seed, off=frozenset({feature}))
                self.assertFalse(floating.known_shapes(steered))

    def test_fixed_campaign_shapes_are_enabled_with_off_known(self):
        for generator, features in (
                (integer, ('initinterpret', 'bfunsignedshift')),
                (floating, ('finitinterpret', 'fconstdivzero', 'fupdatepair'))):
            for feature in features:
                with self.subTest(generator=generator.__name__, feature=feature):
                    self.assertIn(feature, generator.FEATURES)
                    self.assertNotIn(feature, generator.KNOWN_BUGS)

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


class FrontendProbeTests(unittest.TestCase):
    def test_default_campaign_keeps_main_seeds_and_reports_frontend_failure(self):
        driver = Path(__file__).resolve().parents[1] / 'clang-oracle-fuzz.py'
        with mock.patch.object(sys, 'path', sys.path.copy()):
            run_campaign = runpy.run_path(str(driver))['run_campaign']
        args = SimpleNamespace(gen='int', seeds='120001-120012', work=Path('work'),
                               frontend_off=frozenset({'pp-unsigned'}))
        fuzz = mock.Mock(side_effect=(1, 0))
        with mock.patch.dict(run_campaign.__globals__, {'fuzz': fuzz}):
            self.assertEqual(run_campaign(args, list(range(120001, 120013))), 1)
        probe_args, probe_seeds = fuzz.call_args_list[0].args
        self.assertEqual(probe_args.gen, 'frontend')
        self.assertEqual(probe_args.work, args.work / 'frontend')
        self.assertEqual(probe_args.off, args.frontend_off)
        self.assertEqual(probe_seeds, list(range(120001, 120009)))
        fuzz.assert_called_with(args, list(range(120001, 120013)))

    def test_each_preprocessor_family_covers_both_branches_on_each_route(self):
        observed = {}
        for seed in range(1, 9):
            program = frontend.generate(seed, off=frontend.FEATURES - {'pp-unsigned', 'pp-macro'})
            for probe in program.probes:
                observed.setdefault(probe.name, set()).add(probe.expected[0])
                self.assertEqual('#include' in '\n'.join(probe.prelude),
                                 probe.name.endswith('-include'))
        self.assertEqual(observed, {
            f'pp-{family}-{route}': {0, 1}
            for family in ('macro', 'unsigned')
            for route in ('if', 'elif', 'include')
        })

    def test_literal_cases_stay_reproducible_when_preprocessor_cases_are_disabled(self):
        full = frontend.generate(1)
        literals = frontend.generate(1, off=frontend.FEATURES - {'literal-types'})
        self.assertEqual(literals.probes, tuple(
            probe for probe in full.probes if probe.name.startswith('literal-')))
        self.assertIn('literal-word-octal 1 1 1\n', literals.expected())
        self.assertIn('literal-word-octal-macro 1 1 1\n', literals.expected())
        direct = next(probe for probe in literals.probes if probe.name == 'literal-word-octal')
        self.assertIn('sizeof(int) != 2', direct.expressions[2])

    def test_consecutive_seeds_rotate_through_all_broader_families(self):
        for seed, family in enumerate(frontend.BROAD_GENERATORS, start=0):
            with self.subTest(family=family):
                full = frontend.generate(seed)
                isolated = frontend.generate(seed, off=frontend.FEATURES - {family})
                self.assertTrue(isolated.probes)
                self.assertEqual(full.probes[-len(isolated.probes):], isolated.probes)
                self.assertEqual(len({p.name for p in full.probes}), len(full.probes))

    def test_export_keeps_nested_include_files_and_rejects_conflicts(self):
        driver = Path(__file__).resolve().parents[1] / 'clang-oracle-fuzz.py'
        with mock.patch.object(sys, 'path', sys.path.copy()):
            write_program = runpy.run_path(str(driver))['write_program']
        program = frontend.generate(9, off=frontend.FEATURES - {'pp-includes'})
        with TemporaryDirectory() as folder:
            source = Path(folder) / 'probe.c'
            write_program(program, source, export=True)
            self.assertEqual(len(program.headers()), 2)
            for name, body in program.headers():
                self.assertEqual(source.with_name(name).read_text(), body)
            header = source.with_name(program.headers()[0][0])
            header.write_text('existing file')
            with self.assertRaises(FileExistsError):
                write_program(program, source, export=True)
            self.assertEqual(header.read_text(), 'existing file')
