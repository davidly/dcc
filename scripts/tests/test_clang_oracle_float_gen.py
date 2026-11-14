"""Restored float features must remain eligible for differential execution."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
try:
    import clang_oracle_float_gen as gen
finally:
    sys.path.pop(0)


class FloatKnownShapeTests(unittest.TestCase):
    def test_only_disabled_shapes_block_oracle_execution(self):
        integer = gen.E("const", a=-72, t=gen.I32)
        expressions = {
            "fretconv": integer,
            "fu32cast": gen.E("cast", t=gen.F32, k=[
                gen.E("cast", t=gen.U32, k=[integer])]),
        }
        for feature, expression in expressions.items():
            program = gen.Program(0)
            function = gen.Func("probe", gen.F32, [], "helper")
            function.body.stmts.append(gen.S("ret", e=[expression]))
            program.funcs.append(function)
            for disabled in (frozenset({feature}), frozenset(),
                             frozenset({"fdivround"})):
                with self.subTest(feature=feature, disabled=disabled):
                    program.off = disabled
                    expected = {feature} if feature in disabled else set()
                    self.assertEqual(gen.known_shapes(program), expected)

    def test_local_float_array_loop_context_is_narrowly_steered(self):
        generator = gen.Gen(0, off=frozenset({"farrloopindex"}))
        fn = gen.Func("probe", gen.F32, [], "test")
        generator.prog.funcs.append(fn)
        array = gen.Decl("a", gen.F32, "local", dims=(4,))
        index = gen.Decl("i", gen.I16, "local")
        lv = gen.E("idx", [gen.E("var", a=index)], a=array)
        branch = gen.Block()
        branch.stmts.append(gen.S("expr", [gen.E("asg", [
            lv, gen.E("const", a=1.0, t=gen.F32)], a="=")]))
        nested = gen.Block()
        nested.stmts.append(gen.S("while", [gen.E("var", a=index)], [gen.Block()]))
        fn.body.stmts.extend([
            gen.S("while", [gen.E("var", a=index)], [nested]),
            gen.S("if", [gen.E("var", a=index)], [branch]),
            gen.S("while", [gen.E("var", a=index)], [gen.Block()]),
        ])
        self.assertEqual(gen.known_shapes(generator.prog), {"farrloopindex"})
        array.storage = "global"
        self.assertEqual(gen.known_shapes(generator.prog), set())
        array.storage = "local"
        later = fn.body.stmts.pop()
        self.assertEqual(gen.known_shapes(generator.prog), set())
        fn.body.stmts.append(later)
        generator.steer()
        self.assertEqual(gen.known_shapes(generator.prog), set())
        self.assertEqual(lv.k[0].op, "const")
