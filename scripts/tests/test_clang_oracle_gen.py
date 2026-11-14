"""Keep parked byte shapes separate from restored self-operands and postfix words."""

from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
try:
    import clang_oracle_gen as gen
finally:
    sys.path.pop(0)


class IntegerKnownShapeTests(unittest.TestCase):
    def test_byte_self_steer_preserves_word_and_logical_operators(self):
        for typ in (gen.I8, gen.U8, gen.I16, gen.U16, gen.I32):
            for kind, operator in (("bin", "-"), ("bin", "&"),
                                   ("asg", "-="), ("asg", "&="),
                                   ("bin", "+"), ("bin", "*"),
                                   ("bin", "&&"), ("asg", "=")):
                with self.subTest(typ=typ, kind=kind, operator=operator):
                    generator = gen.Gen(0, off=frozenset(gen.KNOWN_BUGS) | {"peepbyteself"})
                    variable = gen.E("var", a=gen.Decl("x", typ, "local"))
                    expression = generator.mk(kind, [variable, variable], operator)
                    should_steer = typ.bits == 8 and operator not in ("&&", "=")
                    self.assertEqual(expression.k[1].op,
                                     "const" if should_steer else "var")
                    enabled = gen.Gen(0, off=frozenset(gen.KNOWN_BUGS)).mk(
                        kind, [variable, variable], operator)
                    self.assertEqual(enabled.k[1].op, "var")

    def test_reducer_detects_disabled_byte_shape_only(self):
        for typ in (gen.I8, gen.U8, gen.I16):
            variable = gen.E("var", a=gen.Decl("x", typ, "local"))
            for kind, operator in (("bin", "-"), ("asg", "&="), ("bin", "&&")):
                program = gen.Program(0, off={"peepbyteself"})
                function = gen.Func("probe", gen.I16, [], "helper")
                function.body.stmts.append(gen.S("expr", e=[
                    gen.E(kind, [variable, variable], operator)]))
                program.funcs.append(function)
                with self.subTest(typ=typ, kind=kind, operator=operator):
                    expected = {"peepbyteself"} if typ.bits == 8 and operator != "&&" else set()
                    self.assertEqual(gen.known_shapes(program), expected)
                    program.off = frozenset()
                    self.assertEqual(gen.known_shapes(program), set())

    def test_logicinit_rewrite_does_not_reintroduce_byte_self_and(self):
        generator = gen.Gen(0, off=frozenset(gen.KNOWN_BUGS) | {"peepbyteself"})
        variable = gen.E("var", a=gen.Decl("x", gen.U8, "local"))
        expression = generator.mk("bin", [variable, variable], "&&")
        stored = generator.stored(expression, gen.U8, "local")
        self.assertEqual(stored.k[1].op, "const")
        self.assertFalse(gen.peep_byte_self(stored.a, stored.k))

    def test_postfix_steer_restores_words_but_preserves_byte_and_wide_guards(self):
        for typ in (gen.I8, gen.U8, gen.I16, gen.U16, gen.I32, gen.U32):
            with self.subTest(typ=typ):
                generator = gen.Gen(0, off=frozenset(gen.KNOWN_BUGS))
                declaration = gen.Decl("x", typ, "local")
                function = gen.Func("probe", gen.I16, [], "helper")
                context = gen.Ctx(function, True)
                with mock.patch.object(generator, "pick", return_value="sfx"), \
                     mock.patch.object(generator, "victim", return_value=declaration), \
                     mock.patch.object(generator.rng, "random", side_effect=(1, 0, 1)):
                    expression = generator.gen_expr(context, 1)
                self.assertEqual(expression.op, "incdec")
                self.assertEqual(expression.a[1], typ.bits != 16)
                expression.a = ("++", False)
                program = gen.Program(0, off={"postincvalue"})
                function.body.stmts.append(gen.S("expr", e=[
                    gen.E("bin", [expression, gen.E("const", a=1, t=gen.I16)], "+")]))
                program.funcs.append(function)
                self.assertEqual(gen.known_shapes(program),
                                 {"postincvalue"} if typ.bits == 8 else set())

    def test_known_steers_restore_cross_type_word_assignments(self):
        for source_type, target_type in ((gen.I16, gen.U16), (gen.U16, gen.I16)):
            with self.subTest(source_type=source_type, target_type=target_type):
                rhs = gen.E("var", a=gen.Decl("source", source_type, "local"))
                generator = gen.Gen(0, off=frozenset(gen.KNOWN_BUGS))
                self.assertIs(generator.stored(rhs, target_type, "local"), rhs)
                disabled = gen.Gen(0, off={"crosstypeasg"})
                steered = disabled.stored(rhs, target_type, "local")
                self.assertEqual(steered.op, "cast")
                self.assertEqual(steered.k[0].t, gen.I32)
