"""Keep the parked byte peephole shape separate from restored self-operands."""

from pathlib import Path
import sys
import unittest

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
                    generator = gen.Gen(0, off=frozenset(gen.KNOWN_BUGS))
                    variable = gen.E("var", a=gen.Decl("x", typ, "local"))
                    expression = generator.mk(kind, [variable, variable], operator)
                    should_steer = typ.bits == 8 and operator not in ("&&", "=")
                    self.assertEqual(expression.k[1].op,
                                     "const" if should_steer else "var")
                    enabled = gen.Gen(0).mk(kind, [variable, variable], operator)
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
        generator = gen.Gen(0, off=frozenset(gen.KNOWN_BUGS))
        variable = gen.E("var", a=gen.Decl("x", gen.U8, "local"))
        expression = generator.mk("bin", [variable, variable], "&&")
        stored = generator.stored(expression, gen.U8, "local")
        self.assertEqual(stored.k[1].op, "const")
        self.assertFalse(gen.peep_byte_self(stored.a, stored.k))
