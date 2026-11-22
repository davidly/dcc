"""Keep parked byte shapes separate from restored wide and scalar value forms."""

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
    def test_restored_block_word_cast_and_decrement_keep_their_shapes(self):
        generator = gen.Gen(0, off=frozenset(gen.KNOWN_BUGS))
        word = gen.E('var', a=gen.Decl('x', gen.U16, 'block'))
        decrement = generator.mk('incdec', [word], ('--', False))
        self.assertEqual(decrement.op, 'incdec')
        cast = gen.E('cast', [word], t=gen.I16)
        wide = gen.E('var', a=gen.Decl('b', gen.I32, 'block'))
        for operands in ([cast, wide], [wide, cast]):
            expression = generator.mk('bin', operands, '>')
            self.assertIn(cast, expression.k)
            self.assertIs(cast.t, gen.I16)

    def test_restored_block_pointer_walks_include_words_but_steer_longs(self):
        for typ, off in ((gen.U16, frozenset(gen.KNOWN_BUGS)),
                         (gen.I32, frozenset()), (gen.U32, frozenset())):
            with self.subTest(typ=typ):
                generator = gen.Gen(0, off=off)
                function = gen.Func('probe', gen.I16, [], 'helper')
                context = gen.Ctx(function, pure=True)
                context.arrs.append(gen.Decl('a', typ, 'local', dims=(2,)))
                with mock.patch.object(generator, 'gen_block', return_value=gen.Block()):
                    generator.st_walk(context, 1, function.body)
                self.assertEqual(len(function.body.stmts), 1)
                pointer = function.body.stmts[0].b[0].decls[0]
                self.assertIs(pointer.t.to, typ)
                self.assertEqual(pointer.storage, 'block')
    def test_restored_block_word_initializers_keep_optional_steer(self):
        cases = ((gen.I16, gen.U16, 'block', True),
                 (gen.U16, gen.I16, 'block', True),
                 (gen.I16, gen.I16, 'block', False),
                 (gen.U16, gen.U16, 'block', False),
                 (gen.I16, gen.U16, 'local', False),
                 (gen.I32, gen.U32, 'block', False))
        for target, source, storage, blocked in cases:
            for cast in (False, True):
                with self.subTest(target=target, source=source,
                                  storage=storage, cast=cast):
                    expression = gen.E('var', a=gen.Decl('x', source, 'param'))
                    if cast:
                        expression = gen.E('cast', [expression], t=target)
                    # Isolate the optional blockwordinit steer from the
                    # reopened BUG-016 initializer-interpretation steer.
                    enabled = gen.Gen(0, off=frozenset(gen.KNOWN_BUGS) - {
                        'initinterpret'}).initialiser(
                        None, target, expression, storage)
                    steered = gen.Gen(0, off={'blockwordinit'}).initialiser(
                        None, target, expression, storage)
                    self.assertIsNone(enabled[1])
                    self.assertEqual(steered[1] is expression, blocked)
                    program = gen.Program(0, off={'blockwordinit'})
                    function = gen.Func('probe', gen.I16, [], 'helper')
                    function.body.decls.append(gen.Decl(
                        'b', target, storage, init=expression))
                    program.funcs.append(function)
                    self.assertEqual(gen.known_shapes(program),
                                     {'blockwordinit'} if blocked else set())
                    function.body.decls[0].init = steered[0]
                    self.assertEqual(gen.known_shapes(program), set())

    def test_restored_wide_increment_add_preserves_operands(self):
        cases = ((gen.I32, 'local', '++', True, False, True),
                 (gen.U32, 'param', '++', True, False, True),
                 (gen.I32, 'local', '+=', True, False, True),
                 (gen.I16, 'local', '++', True, False, False),
                 (gen.I32, 'global', '++', True, False, False),
                 (gen.I32, 'local', '--', True, False, False),
                 (gen.I32, 'local', '++', False, False, False),
                 (gen.I32, 'local', '++', True, True, False))
        for typ, storage, operator, prefix, literal, blocked in cases:
            for reverse in (False, True):
                with self.subTest(typ=typ, storage=storage, operator=operator,
                                  prefix=prefix, literal=literal, reverse=reverse):
                    local = gen.E('var', a=gen.Decl('x', typ, storage))
                    if operator == '+=':
                        update = gen.E('asg', [local, gen.E('const', a=1, t=gen.I16)], '+=')
                    else:
                        update = gen.E('incdec', [local], (operator, prefix))
                    other = (gen.E('const', a=7, t=gen.I32) if literal else
                             gen.E('var', a=gen.Decl('y', gen.I32, 'global')))
                    operands = [other, update] if reverse else [update, other]
                    enabled = gen.Gen(0, off=frozenset(gen.KNOWN_BUGS)).mk('bin', operands, '+')
                    steered = gen.Gen(0, off={'wideincadd'}).mk('bin', operands, '+')
                    self.assertEqual(enabled.a, '+')
                    self.assertEqual(steered.a, '-' if blocked else '+')
                    self.assertIs(steered.k[1 if reverse else 0], update)
                    program = gen.Program(0, off={'wideincadd'})
                    function = gen.Func('probe', gen.I16, [], 'helper')
                    function.body.stmts.append(gen.S('expr', e=[enabled]))
                    program.funcs.append(function)
                    self.assertEqual(gen.known_shapes(program),
                                     {'wideincadd'} if blocked else set())
                    function.body.stmts[0].e[0] = steered
                    self.assertEqual(gen.known_shapes(program), set())

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

    def test_logicbyteinit_rewrite_does_not_reintroduce_byte_self_and(self):
        generator = gen.Gen(0, off=frozenset(gen.KNOWN_BUGS) |
                            {"logicbyteinit", "peepbyteself"})
        variable = gen.E("var", a=gen.Decl("x", gen.U8, "local"))
        expression = generator.mk("bin", [variable, variable], "&&")
        stored = generator.stored(expression, gen.U8, "local")
        self.assertEqual(stored.k[1].op, "const")
        self.assertFalse(gen.peep_byte_self(stored.a, stored.k))

    def test_byte_logical_steer_restores_word_and_block_long_stores(self):
        for typ in (gen.I8, gen.U8, gen.I16, gen.U16, gen.I32, gen.U32):
            for storage in ("local", "block", "global"):
                for operator in ("&&", "||"):
                    with self.subTest(typ=typ, storage=storage, operator=operator):
                        generator = gen.Gen(0, off={"logicbyteinit"})
                        rhs = gen.E("bin", [gen.E("const", a=1, t=gen.I16),
                                           gen.E("const", a=0, t=gen.I16)], operator)
                        declaration = gen.Decl("value", typ, storage, init=rhs)
                        function = gen.Func("probe", gen.I16, [], "helper")
                        function.body.decls.append(declaration)
                        generator.prog.funcs.append(function)
                        guarded = typ.bits == 8 and storage != "global"
                        self.assertEqual(gen.known_shapes(generator.prog),
                                         {"logicbyteinit"} if guarded else set())
                        stored = generator.stored(rhs, typ, storage, init=True)
                        self.assertEqual(stored.a,
                                         ("&" if operator == "&&" else "|")
                                         if guarded else operator)
                        self.assertEqual(gen.known_shapes(generator.prog), set())
                        if typ.bits == 8:
                            restored = gen.Gen(0, off=frozenset(gen.KNOWN_BUGS))
                            rhs = gen.E("bin", [gen.E("const", a=1, t=gen.I16),
                                               gen.E("const", a=0, t=gen.I16)], operator)
                            declaration.init = restored.stored(rhs, typ, storage, init=True)
                            restored.prog.funcs.append(function)
                            self.assertEqual(declaration.init.a, operator)
                            self.assertEqual(gen.known_shapes(restored.prog), set())

    def test_postfix_steer_restores_all_widths(self):
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
                self.assertFalse(expression.a[1])
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


class SizeofTests(unittest.TestCase):
    def context(self):
        function = gen.Func("probe", gen.I16, [], "helper")
        context = gen.Ctx(function, True)
        context.ints.append(gen.Decl("x", gen.I16, "local"))
        context.arrs.append(gen.Decl("a", gen.U8, "local", dims=(4,)))
        context.arrs.append(gen.Decl("p", gen.I16, "param", dims=(4,)))
        return context

    def test_sizeof_names_only_host_sized_objects_or_types(self):
        generator = gen.Gen(0)
        seen = set()
        for _ in range(200):
            expression = generator.gen_sizeof(self.context())
            self.assertEqual(expression.op, "sizeof")
            self.assertIs(gen.ty(expression), gen.U16)
            self.assertTrue(gen.is_constant(expression))
            if isinstance(expression.a, gen.Decl):
                self.assertNotEqual(expression.a.storage, "param")
                seen.add(expression.a.name)
            else:
                seen.add(expression.a.cname)
        self.assertIn("x", seen)
        self.assertIn("a", seen)
        self.assertNotIn("p", seen)
        renderer = gen.Renderer()
        self.assertEqual(renderer.ex(gen.E("sizeof", a=gen.Decl("a", gen.U8, "local", dims=(4,)),
                                           t=gen.U16)), "sizeof(a)")
        self.assertEqual(renderer.ex(gen.E("sizeof", a=gen.I32, t=gen.U16)), "sizeof(int32_t)")

    def test_bare_sizeof_is_kept_beside_words_and_cast_beside_longs(self):
        generator = gen.Gen(0)
        size = gen.E("sizeof", a=gen.I16, t=gen.U16)
        word = gen.E("var", a=gen.Decl("w", gen.I16, "local"))
        wide = gen.E("var", a=gen.Decl("l", gen.I32, "local"))
        kept = generator.mk("bin", [word, size], "<")
        self.assertIs(kept.k[1], size)
        self.assertEqual(gen.Renderer().ex(kept), "(((uint16_t)w) < sizeof(int16_t))")
        for op, operands in (("bin", [wide, size]), ("bin", [size, wide]),
                             ("cond", [word, size, wide]), ("asg", [wide, size])):
            with self.subTest(op=op):
                expression = generator.mk(op, list(operands), "/" if op == "bin" else "=")
                cast = [k for k in expression.k if k.op == "cast"]
                self.assertEqual(len(cast), 1)
                self.assertIs(cast[0].k[0], size)
                self.assertIs(cast[0].t, gen.U16)
        shifted = generator.mk("bin", [size, wide], "<<")
        self.assertIs(shifted.k[0], size)

    def test_sizeof_is_off_with_its_feature(self):
        generator = gen.Gen(0, off={"sizeof"})
        for _ in range(100):
            self.assertNotEqual(generator.gen_leaf(self.context(), 1).op, "sizeof")
