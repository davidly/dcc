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
