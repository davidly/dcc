#!/usr/bin/env python3
"""Float-centric program generator for `clang-oracle-fuzz.py --gen float`.

dcc's `float` is a 32-bit IEEE single implemented by the runtime, and it is
the only floating type. This module reuses the program model, renderer and
driver of clang_oracle_gen.py but generates programs whose values flow
through `float`: float globals, arrays, struct members and pointers; float
parameters and results; arithmetic, compound assignment and ++/--;
comparisons and logic on floats; `?:` with float arms; and conversions in
both directions between `float` and every integer type.

Results are compared by IEEE bit pattern (see FLOAT_HARNESS), so the host
must compute in strict single precision: the driver passes
`-ffp-contract=off`. Programs are kept away from infinities, NaNs and
subnormals by the checked build's ck_f, and every float-to-integer
conversion is range checked; a tripped node is repaired like an integer one.

The integer side is kept deliberately plain (function-scope locals,
same-signedness initialisers, no bit-fields, no post-increment values) so the
logged integer defects do not drown float findings.
"""
from __future__ import annotations

import random

from clang_oracle_gen import (  # noqa: F401  (re-exported for the driver)
    init_keeps_type,
    ARITH, CMP, LOGIC, DIV, E, S, Block, Func, Program, Decl, Field, StructType, PtrType,
    IntType, I8, U8, I16, U16, I32, U32, F32, INT_TYPES, is_int, is_float, is_scalar, ty,
    promote, common, Renderer, clone, expr_slots, slot_get, slot_set, all_blocks, mk_const,
    float_text, is_bitfield, same_variable)
import clang_oracle_gen as base

FARITH = ('+', '-', '*')

FPOOL = [0.0, 1.0, -1.0, 0.5, -0.5, 2.0, 0.25, 0.1, 0.2, 0.3, 3.0, 4.0, 10.0, 100.0, 1000.0,
         1e4, 65536.0, 32768.0, 32767.0, 65535.0, 0.001, 1.5, 7.0, 123.456, 3.14159, 2.71828,
         1e6, 255.0, 256.0, 16777216.0, -32768.0, 0.999, 1e9, 0.75, 1e-3, -2.5, 6.02e5,
         2147483648.0, 4294967296.0, 0.0625, 1e-6]

FEATURES = frozenset((
    # program shape
    'farr', 'fstruct', 'fptr', 'fcall', 'fparam', 'fret', 'fblock', 'fif', 'ffor', 'fwhile',
    'fret_early', 'iparam',
    # expression forms
    'fdiv', 'fneg', 'fcond', 'fcmp', 'flogic', 'fnot', 'ftruth', 'f2i', 'i2f', 'fcassign',
    'fincdec', 'fincdecval', 'fasgval', 'fconst', 'mixarith', 'f2u', 'f2i8', 'i8', 'u32',
    'fchain', 'fcmpconst',
    # forms that hit a logged dcc defect; see KNOWN_BUGS
    'fdivround', 'fconstfold', 'icasgf', 'fblockneg', 'fnegcmp', 'fretconv',
    'fintprodadd', 'fu32cast', 'fnegzero_not', 'fparamcmp', 'fblockint', 'finittype',
    'fconstjump', 'fselfop', 'fconstcond', 'floopcallarg', 'fptrparam'))

# Features switched off by `--off known`, with the entry in compiler-bugs.md
# that each one would otherwise keep rediscovering.
KNOWN_BUGS = {
    'fdivround': 'BUG-039',
    'fincdecval': 'BUG-041',
    'fincdec': 'BUG-044',
    'fconstfold': 'BUG-042',
    'icasgf': 'BUG-043',
    'fblockneg': 'BUG-015',
    'fnegcmp': 'BUG-045',
    'fintprodadd': 'BUG-047',
    'fnegzero_not': 'BUG-049',
    'fparamcmp': 'BUG-020',
    'fblockint': 'BUG-015',
    'finittype': 'BUG-016',
    'fconstjump': 'BUG-019',
    'fselfop': 'BUG-026',
    'fconstcond': 'BUG-052',
    'floopcallarg': 'BUG-036',
}


def is_fconst(e):
    """A float expression made only of constants (dcc has to fold it)."""
    if e.op == 'const':
        return True
    if e.op == 'cast' and e.t is F32:
        return is_fconst(e.k[0])
    if e.op in ('un', 'bin') and ty(e) is F32:
        return all(is_fconst(k) for k in e.k)
    return False


def is_anyconst(e, seen_float=None):
    """e is made only of constants; seen_float (a list) gets a mark when a
    float constant is among them."""
    if e.op == 'const':
        if ty(e) is F32 and seen_float is not None:
            seen_float.append(True)
        return True
    if e.op in ('cast', 'un', 'bin'):
        return all(is_anyconst(k, seen_float) for k in e.k)
    return False


def needs_fconst_fold(e):
    """e is a shape BUG-042 rejects: `-`/`!` of a float constant, `&&`/`||`
    with a float constant operand, or `&&`/`||` of nothing but constants
    when a float constant is involved (`(1.0f > 2.0f) && (1 == 1)`)."""
    if e.op == 'un':
        return ty(e.k[0]) is F32 and is_fconst(e.k[0])
    if e.op == 'bin' and e.a in LOGIC:
        if any(is_fconst(k) for k in e.k):
            return True
        seen = []
        return is_anyconst(e, seen) and bool(seen)
    return False


def is_neg_fconst(e):
    """A negative float constant as written: `-1.0f`, `(-1.0f)`, `-(1.0f)`."""
    if e.op == 'const':
        return ty(e) is F32 and e.a < 0
    return e.op == 'un' and e.a == '-' and is_fconst(e.k[0])


def is_negcmp(e):
    """e is a shape BUG-045 rejects: a comparison whose right operand is a
    float binop with a constant left operand (`x < -1.0f - n`,
    `1.0f <= 3.0f * f`)."""
    if e.op != 'bin' or e.a not in CMP:
        return False
    rhs = e.k[1]
    return rhs.op == 'bin' and rhs.a in ('+', '-', '*', '/') and ty(rhs) is F32 \
        and is_anyconst(rhs.k[0])


def is_retconv(fn, e):
    """`return e` in fn needs an implicit long<->float conversion."""
    if fn.ret is None or e is None:
        return False
    t = ty(e)
    if fn.ret is F32:
        return is_int(t) and t.bits == 32
    return fn.ret.bits == 32 and t is F32


FLIP = {'<': '>', '>': '<', '<=': '>=', '>=': '<=', '==': '==', '!=': '!='}


def is_fprod(e):
    """A float product."""
    return e.op == 'bin' and e.a == '*' and ty(e) is F32


def is_intprodadd(e):
    """e is a shape BUG-047 miscompiles: `f + (x * y)` or `f += x * y`.
    Only some operand kinds fail (an integer x, or a local/parameter y
    with a non-local x), but the kinds depend on register placement, so
    every float product on the right of a `+` counts."""
    if e.op == 'bin' and e.a == '+' and ty(e) is F32:
        return is_fprod(e.k[1])
    return e.op == 'asg' and e.a == '+=' and ty(e.k[0]) is F32 and is_fprod(e.k[1])


def is_u32s(e):
    """`(uint32_t)x` with x a signed 32-bit expression (BUG-048), or
    `(int32_t)(uint32_t)x` with x signed and narrow (BUG-050)."""
    if e.op != 'cast':
        return False
    if e.t is U32 and ty(e.k[0]) is I32:
        return True
    if e.t is not I32:
        return False
    inner = e.k[0]
    while inner.op == 'cast' and inner.t is I32:
        inner = inner.k[0]      # `(int32_t)(int32_t)…`
    if ty(inner) is U32:
        return True     # arithmetic, folded or not (seed 2036), or a variable (3792)
    while inner.op == 'bin' and inner.a in ('+', '-') and is_anyconst(inner.k[1]):
        inner = inner.k[0]
    return inner.op == 'cast' and inner.t is U32 \
        and is_int(ty(inner.k[0])) and ty(inner.k[0]).signed and ty(inner.k[0]).bits < 32


def is_wide(t):
    return t is F32 or (is_int(t) and t.bits == 32)


def is_paramcmp(e):
    """e is the BUG-020 facet shape: `v REL (param OP x)` with a 32-bit
    binop on the right, a parameter as its left operand and anything but a
    plain float constant as its right operand (an integer constant or a
    cast of one counts: seed 20063). An `if … else` with a loop, or a
    braced early return (seed 2017), before it makes the compile fail; any
    function counts."""
    if e.op != 'bin' or e.a not in CMP:
        return False
    rhs = e.k[1]
    return rhs.op == 'bin' and rhs.a in ('+', '-', '*', '/') and is_wide(ty(rhs)) \
        and rhs.k[0].op == 'var' and rhs.k[0].a.storage == 'param' \
        and not (rhs.k[1].op == 'const' and rhs.k[1].t is F32) \
        and not (is_anyconst(rhs.k[1]) and ty(rhs) is not F32)


def block_object(e):
    """The nested-block variable e reads: a variable, an element, or the
    value of an assignment to one (`(b = 1.18f)` behaves like `b`, seeds
    1490 and 1861); else None."""
    if e.op == 'asg':
        e = e.k[0]
    if e.op in ('var', 'idx') and e.a.storage == 'block':
        return e.a
    return None


def is_blockint(e):
    """A nested-block integer variable used as it stands."""
    d = block_object(e)
    return d is not None and is_int(d.t)


def is_blockfloat(e):
    """A nested-block float variable, or a `?:` whose arms are all such
    variables (`fg ? b5 : b6` behaves like `b5`, seed 4750)."""
    if e.op == 'cond':
        return all(is_blockfloat(a) for a in e.k[1:])
    d = block_object(e)
    return d is not None and d.t is F32


def is_paramcond(e):
    """A `?:` with a constant test (seed 2247) or a bare 32-bit parameter
    as an arm (seed 4294): in a loop, with the parameter assigned afterwards,
    the expression accumulates into the parameter (BUG-052)."""
    if e.op != 'cond':
        return False
    if base.is_constant(e.k[0]):
        return True
    return any(a.op == 'var' and a.a.storage == 'param' and is_wide(a.a.t)
               for a in e.k[1:])


def is_ptrparam(e):
    """The address of a parameter: with dccpeep, a later assignment to the
    parameter can be dropped as dead (BUG-053, seed 3036)."""
    return e.op == 'addrof' and e.a.storage == 'param'


def is_loopcallarg(e):
    """A call (including the repair's fclamp) with a non-global float
    variable, or an assignment to one, as an argument: inside a loop the
    BUG-036 shape for floats (seeds 3025 and 3288), and the local is garbage
    or the assigned constant afterwards."""
    if e.op not in ('call', 'fclamp'):
        return False
    for a in e.k:
        v = a.k[0] if a.op == 'asg' else a
        if v.op == 'var' and v.a.storage != 'global' and v.a.t is F32:
            return True
    return False


def is_selfop(e):
    """`b OP b` or `b OP= b` with one non-global float or 32-bit variable
    as both operands: the BUG-026 shape for floats (seeds 1681 and 3223)."""
    if e.op not in ('bin', 'asg') or e.a in LOGIC:
        return False
    a, b = e.k
    return same_variable(a, b) and a.a.storage != 'global' and is_wide(a.a.t)


def blockint_operands(e):
    """Indexes of e's operands that are the BUG-015 facet shapes: a
    nested-block integer variable converted to float by its context
    (arithmetic, comparison, `?:` arm, compound assignment, or a store to
    a float element, member or pointee), or an integer operand of float
    arithmetic/comparison/compound assignment whose other operand is a
    nested-block float variable. A call argument and a store to a plain
    float variable convert correctly, and so does `(float)b`."""
    if e.op in ('call', 'cast'):
        return []
    if e.op == 'asg' and e.a == '=' and e.k[0].op == 'var':
        return []
    out = [i for i, (k, want) in enumerate(zip(e.k, float_contexts(e)))
           if want is F32 and is_blockint(k)]
    if (e.op == 'bin' and e.a in ('+', '-', '*', '/') + CMP) or \
            (e.op == 'asg' and e.a != '='):
        if any(is_blockfloat(k) for k in e.k):
            out += [i for i, k in enumerate(e.k) if is_int(ty(k)) and i not in out]
    return out


def simple_const(v, t):
    """A constant of type t worth v (the reducer's replacement value)."""
    if t is F32:
        return E('const', a=float(v), t=F32)
    ct = t if t.bits >= 16 else (I16 if t.signed else U16)
    return mk_const(v, ct)


# ---------------------------------------------------------------- shapes

def known_shapes(prog):
    """Disabled bug features whose shapes survived steering or repair."""
    found = set()
    for slot in expr_slots(prog):
        e = slot_get(slot)
        if e.op == 'bin' and e.a == '/' and ty(e) is F32:
            found.add('fdivround')
        if e.op == 'asg' and e.a == '/=' and (ty(e.k[0]) is F32 or ty(e.k[1]) is F32):
            found.add('fdivround')
    # the value of a float ++/--: an incdec that is not a whole expression
    # statement or a for step
    def fincdec(e):
        return e is not None and e.op == 'incdec' and ty(e) is F32

    for slot in expr_slots(prog):
        e = slot_get(slot)
        if fincdec(e):
            found.add('fincdec')
        if any(fincdec(k) for k in e.k):
            found.add('fincdecval')
        if needs_fconst_fold(e):
            found.add('fconstfold')
        if e.op == 'asg' and e.a != '=' and is_int(ty(e.k[0])) and ty(e.k[1]) is F32:
            found.add('icasgf')
        if e.op == 'un' and e.a == '-' and is_blockfloat(e.k[0]):
            found.add('fblockneg')
        if is_selfop(e):
            found.add('fselfop')
        if is_loopcallarg(e):
            found.add('floopcallarg')
        if is_paramcond(e):
            found.add('fconstcond')
        if is_paramcmp(e):
            found.add('fparamcmp')
        if is_negcmp(e):
            found.add('fnegcmp')
        if is_intprodadd(e):
            found.add('fintprodadd')
        if e.op == 'un' and e.a == '!' and ty(e.k[0]) is F32:
            found.add('fnegzero_not')
        if blockint_operands(e):
            found.add('fblockint')
        for k, want in zip(e.k, float_contexts(e)):
            if want is F32 and is_u32s(k):
                found.add('fu32cast')
    for b in all_blocks(prog):
        for d in b.decls:
            if d.t is F32 and isinstance(d.init, E) and is_u32s(d.init):
                found.add('fu32cast')
            if d.storage == 'block' and is_int(d.t) and isinstance(d.init, E) \
                    and not init_keeps_type(d.init, d.t):
                found.add('finittype')
    for b in all_blocks(prog):
        if any(fincdec(d.init) for d in b.decls if isinstance(d.init, E)):
            found.add('fincdecval')
        for st in b.stmts:
            if base.const_jump(st):
                found.add('fconstjump')
            roots = [st.e[0]] if st.op == 'for' else ([] if st.op == 'expr' else st.e)
            if st.op == 'for' and st.a is not None and isinstance(st.a.init, E):
                roots.append(st.a.init)
            if any(fincdec(e) for e in roots):
                found.add('fincdecval')
            tests = [st.e[1]] if st.op == 'for' else (st.e if st.op in ('if', 'while') else [])
            if any(e is not None and is_fconst(e) for e in tests):
                found.add('fconstfold')
    for fn in prog.funcs:
        stack = [fn.body]
        while stack:
            b = stack.pop()
            for st in b.stmts:
                if st.op == 'ret' and st.e and is_retconv(fn, st.e[0]):
                    found.add('fretconv')
                stack.extend(sub for sub in st.b if sub is not None)
    return found & prog.off


def float_contexts(e):
    """For each operand of e, the type it is converted to (F32 when the
    operand is implicitly or explicitly made a float), else None."""
    if e.op == 'cast':
        return [e.t]
    if e.op == 'bin' and e.a in ('+', '-', '*', '/'):
        t = ty(e)
        return [t, t]
    if e.op == 'bin' and e.a in CMP:
        t = common(ty(e.k[0]), ty(e.k[1]))
        return [t, t]
    if e.op == 'asg':
        return [None, ty(e.k[0])]
    if e.op == 'cond':
        t = ty(e)
        return [None, t, t]
    if e.op == 'call':
        return [p.t for p in e.a.params]
    return [None] * len(e.k)


# ---------------------------------------------------------------- repair

def clamp(x, bound):
    """x limited to [-bound, bound] through the program's fclamp helper.
    A bare non-global variable is passed as `x + 0.0f`: fclamp is a call, and
    a 32-bit local passed directly to a call in a loop is BUG-036 (seed
    3288), which the repair must not build."""
    if x.op == 'var' and x.a.storage != 'global' and ty(x) is F32:
        x = E('bin', [x, E('const', a=0.0, t=F32)], '+')
    return E('fclamp', [x, E('const', a=bound, t=F32)])


def has_effect(e):
    if e.op in ('asg', 'incdec', 'call'):
        return True
    return any(has_effect(k) for k in e.k)


def mentions(e, decl):
    if e.op in ('var', 'idx', 'pidx', 'aend', 'addrof') and e.a is decl:
        return True
    if e.op == 'addr' and e.a[0] is decl:
        return True
    return any(mentions(k, decl) for k in e.k)


def tame(x, bound):
    if x.op == 'fclamp':
        x.k[1] = E('const', a=min(bound, x.k[1].a) / 4.0, t=F32)
        return x
    return clamp(x, bound)


def f2i_bound(t):
    return {8: 100.0, 16: 30000.0, 32: 2.0e9}[t.bits]


def repair_node(node):
    """Make a tripped float node defined. Returns False when it cannot."""
    op = node.op
    t = ty(node)
    if op == 'bin' and t is F32:
        node.k[1] = E('const', a=1.5 if node.a != '*' else 0.5, t=F32)
        return True
    if op == 'cast' and is_int(t) and ty(node.k[0]) is F32:
        node.k[0] = tame(node.k[0], f2i_bound(t))
        return True
    if op == 'asg':
        lv, rhs = node.k
        lt = ty(lv)
        if lt is F32:
            if node.a == '=':
                return False
            node.a, node.k = '=', [lv, E('const', a=2.5, t=F32)]
            return True
        if ty(rhs) is F32 and is_int(lt):
            if node.a != '=':
                node.a = '='
            node.k[1] = tame(rhs, f2i_bound(lt))
            return True
        return base.repair_node(node)
    if op == 'incdec' and ty(node.k[0]) is F32:
        node.op, node.a, node.k = 'asg', '=', [node.k[0], E('const', a=1.0, t=F32)]
        return True
    return base.repair_node(node)


def repair(prog, trips):
    """Repair every tripped node still present; nodes a parent's repair
    removed from the tree are not failures. Returns the ids left tripping."""
    trips = set(trips)
    failed = set()
    for slot in expr_slots(prog):
        node = slot_get(slot)
        if node.id in trips and not repair_node(node):
            failed.add(node.id)
    present = {slot_get(slot).id for slot in expr_slots(prog)}
    return failed & present


# -------------------------------------------------------------- rendering

class FRenderer(Renderer):
    """The integer renderer, with float-typed nodes rendered plainly in the
    natural source and through ck_f / ck_f2* in the checked one."""

    def f2i(self, t, s, node_id):
        if self.checked:
            return 'ck_f2%s%d(%d, %s)' % ('i' if t.signed else 'u', t.bits, node_id, s)
        return '((%s)%s)' % (t.cname, s)

    def ex_const(self, e):
        if e.t is F32:
            return float_text(e.a)
        return super().ex_const(e)

    def ex_cast(self, e):
        src = ty(e.k[0])
        if e.t is F32:
            return '((float)%s)' % self.ex(e.k[0])
        if src is F32:
            return self.f2i(e.t, self.ex(e.k[0]), e.id)
        return super().ex_cast(e)

    def ex_un(self, e):
        if ty(e.k[0]) is F32:
            return '(%s%s)' % (e.a, self.ex(e.k[0]))
        return super().ex_un(e)

    def ex_bin(self, e):
        ka, kb = e.k
        if ty(ka) is not F32 and ty(kb) is not F32:
            return super().ex_bin(e)
        s = '(%s %s %s)' % (self.ex(ka), e.a, self.ex(kb))
        if self.checked and e.a in FARITH + ('/',):
            return 'ck_f(%d, %s)' % (e.id, s)
        return s

    def ex_fclamp(self, e):
        return 'fclamp(%s, %s)' % (self.ex(e.k[0]), self.ex(e.k[1]))

    def ex_cond(self, e):
        if ty(e) is F32:
            return '(%s ? %s : %s)' % (self.ex(e.k[0]), self.ex(e.k[1]), self.ex(e.k[2]))
        return super().ex_cond(e)

    def ex_asg(self, e):
        lv, rhs = e.k
        lt, rt = ty(lv), ty(rhs)
        if lt is not F32 and rt is not F32:
            return super().ex_asg(e)
        left, right = self.lv(lv), self.ex(rhs)
        if lt is F32:
            if e.a == '=' or not self.checked:
                return '(%s %s %s)' % (left, e.a, right)
            return '(%s = ck_f(%d, %s %s %s))' % (left, e.id, left, e.a[:-1], right)
        # integer target, float value: the conversion is on the stored value
        if e.a == '=':
            if not self.checked:
                return '(%s = %s)' % (left, right)
            return '(%s = %s)' % (left, self.f2i(lt, right, e.id))
        if not self.checked:
            return '(%s %s %s)' % (left, e.a, right)
        return '(%s = %s)' % (left, self.f2i(lt, '(%s %s %s)' % (left, e.a[:-1], right), e.id))

    def ex_incdec(self, e):
        lv = e.k[0]
        if ty(lv) is not F32:
            return super().ex_incdec(e)
        op, prefix = e.a
        left = self.lv(lv)
        if not self.checked:
            return '(%s%s)' % (op, left) if prefix else '(%s%s)' % (left, op)
        d = '1.0f' if op == '++' else '-1.0f'
        if prefix:
            return '(%s = ck_f(%d, %s + %s))' % (left, e.id, left, d)
        return 'ck_fpost(%d, &%s, %s)' % (e.id, left, d)


# -------------------------------------------------------------- generation

class Ctx:
    def __init__(self, fn, pure):
        self.fn, self.pure = fn, pure
        self.floats, self.ints, self.farrs, self.fptrs, self.svars = [], [], [], [], []
        self.prot = set()
        self.loop = 0
        self.names = 0
        self.fresh()

    def fresh(self):
        """Start a full expression: nothing read (used) or written (banned)."""
        self.used, self.banned = set(), set()

    def mark(self):
        return [len(x) for x in (self.floats, self.ints, self.farrs, self.fptrs, self.svars)]

    def release(self, mark):
        for lst, n in zip((self.floats, self.ints, self.farrs, self.fptrs, self.svars), mark):
            del lst[n:]

    def can_write(self, d):
        if d in self.prot:
            return False
        return d.storage != 'global' or not self.pure

    def name(self, prefix):
        self.names += 1
        return '%s%d' % (prefix, self.names)


class Gen:
    def __init__(self, seed, tests=6, size=10, depth=3, off=frozenset()):
        self.rng = random.Random(seed)
        self.prog = Program(seed, off)
        self.prog.floats = True
        self.n_tests, self.size, self.depth = tests, size, depth
        self.off = frozenset(off)
        self.helpers = []

    def on(self, feature):
        return feature not in self.off

    def mk(self, op, k=(), a=None, t=None):
        self.prog.nid += 1
        return E(op, list(k), a, t, self.prog.nid)

    def pick(self, weighted):
        total = sum(w for _, w in weighted)
        x = self.rng.random() * total
        for item, w in weighted:
            x -= w
            if x < 0:
                return item
        return weighted[-1][0]

    # ---- values

    def fvalue(self):
        r = self.rng
        if r.random() < 0.6:
            return F32.wrap(r.choice(FPOOL))
        mag = 10.0 ** r.uniform(-3, 5)
        v = mag if r.random() < 0.7 else -mag
        if r.random() < 0.5:
            v = round(v, r.randint(0, 3))
        return F32.wrap(v)

    def fconst(self):
        return self.mk('const', a=self.fvalue(), t=F32)

    def int_type(self):
        return self.pick([(I8, 2 if self.on('i8') else 0), (U8, 2 if self.on('i8') else 0),
                          (I16, 5), (U16, 4), (I32, 4), (U32, 3 if self.on('u32') else 0)])

    def ivalue(self, t):
        r = self.rng
        x = r.random()
        if x < 0.5:
            v = r.choice([0, 1, 2, 3, 7, 10, 100, 127, 128, 255, 256, 1000, 32767, 32768,
                          65535, 65536, 16777216, 16777217, 2147483647, -1, -2, -100, -128,
                          -32768, -32769, 1234567])
        elif x < 0.8:
            v = r.randint(-300, 300)
        else:
            v = r.getrandbits(32)
        return t.wrap(v)

    def iconst(self, t):
        if t.bits < 16:
            return self.mk('const', a=self.rng.randint(0, 100), t=I16)
        return self.mk('const', a=self.ivalue(t), t=t)

    def small(self, lo, hi):
        return self.mk('const', a=self.rng.randint(lo, hi), t=I16)

    # ---- skeleton

    def gen_structs(self):
        r = self.rng
        fields = [Field('m0', F32), Field('m1', self.int_type())]
        if self.on('farr'):
            fields.append(Field('m2', F32, dims=(2,)))
        fields.append(Field('m3', F32))
        r.shuffle(fields)
        self.prog.structs.append(StructType('S0', fields))

    def init_value(self, t, dims):
        if dims:
            return [self.init_value(t, dims[1:]) for _ in range(dims[0])]
        if isinstance(t, StructType):
            return [self.init_value(f.t, f.dims) for f in t.fields]
        if t is F32:
            return self.fvalue()
        return self.ivalue(t)

    def gen_globals(self):
        r = self.rng
        g = self.prog.globals
        for n in range(r.randint(2, 4)):
            d = Decl('fg%d' % n, F32, 'global', init=self.fvalue())
            d.static = r.random() < 0.6
            g.append(d)
        if self.on('farr'):
            d = Decl('fa', F32, 'global', dims=(8,), init=self.init_value(F32, (8,)))
            d.static = r.random() < 0.6
            g.append(d)
        for n, t in enumerate((I16, U16, I32, U32, I8, U8)):
            if t.bits == 8 and not self.on('i8') or t is U32 and not self.on('u32'):
                continue
            d = Decl('g%d' % n, t, 'global', init=self.ivalue(t))
            d.static = r.random() < 0.6
            g.append(d)
        d = Decl('ia', I16, 'global', dims=(8,), init=self.init_value(I16, (8,)))
        g.append(d)
        if self.on('fstruct'):
            s0 = self.prog.structs[0]
            d = Decl('s0', s0, 'global', init=self.init_value(s0, ()))
            d.static = True
            g.append(d)
            d = Decl('s1', s0, 'global', dims=(4,), init=self.init_value(s0, (4,)))
            d.static = True
            g.append(d)

    def ctx(self, fn, pure):
        c = Ctx(fn, pure)
        for d in self.prog.globals:
            self.expose(c, d)
        return c

    def expose(self, c, d):
        if isinstance(d.t, PtrType):
            c.fptrs.append(d)
        elif isinstance(d.t, StructType):
            if not d.dims:
                c.svars.append(d)
            else:
                c.svars.append(d)
        elif d.dims:
            if d.t is F32:
                c.farrs.append(d)
        elif d.t is F32:
            c.floats.append(d)
        else:
            c.ints.append(d)

    # ---- lvalues and leaves

    def index(self, c, n):
        r = self.rng
        ints = self.sequenced(c, c.ints, False)
        if r.random() < 0.5 or not ints:
            return self.small(0, n - 1)
        return self.mk('var', a=self.touch(c, r.choice(ints), False))

    def sequenced(self, c, decls, write):
        """The objects in decls that this full expression may still read
        (not written in it) or write (not touched in it), so that clang's
        -Wunsequenced never rejects the program."""
        if write:
            return [d for d in decls if c.can_write(d) and d not in c.used]
        return [d for d in decls if d not in c.banned]

    def touch(self, c, d, write):
        """Record a read or write of d in the current full expression."""
        c.used.add(d)
        if write:
            c.banned.add(d)
        return d

    def flvalue(self, c, write):
        """A float lvalue; write asks for one that may be stored to."""
        r = self.rng
        cands = self.sequenced(c, c.floats, write)
        opts = []
        if cands:
            opts.append(('var', 6))
        arrs = self.sequenced(c, c.farrs, write)
        if arrs and self.on('farr'):
            opts.append(('arr', 2))
        if c.fptrs and self.on('fptr') and (not write or not c.pure):
            opts.append(('ptr', 2))
        svars = self.sequenced(c, c.svars, write)
        if svars and self.on('fstruct'):
            opts.append(('fld', 2))
        if not opts:
            return None
        kind = self.pick(opts)
        if kind == 'var':
            return self.mk('var', a=self.touch(c, r.choice(cands), write))
        if kind == 'arr':
            d = self.touch(c, r.choice(arrs), write)
            return self.mk('idx', [self.index(c, d.dims[0])], a=d)
        if kind == 'ptr':
            return self.mk('deref', [self.mk('var', a=r.choice(c.fptrs))])
        d = self.touch(c, r.choice(svars), write)
        ffields = [f for f in d.t.fields if f.t is F32]
        f = r.choice(ffields)
        if d.dims:
            basee = self.mk('idx', [self.index(c, d.dims[0])], a=d)
        else:
            basee = self.mk('var', a=d)
        kids = [basee]
        if f.dims:
            kids.append(self.small(0, f.dims[0] - 1))
        return self.mk('fld', kids, a=f)

    def ilvalue(self, c, write):
        cands = self.sequenced(c, c.ints, write)
        if not cands:
            return None
        return self.mk('var', a=self.touch(c, self.rng.choice(cands), write))

    # ---- expressions

    def fexpr(self, c, depth):
        """A float-typed expression."""
        r = self.rng
        if depth <= 0 or r.random() < 0.25:
            opts = [('lv', 6), ('const', 4 if self.on('fconst') else 0)]
            ints = self.sequenced(c, c.ints, False)
            if ints and self.on('i2f'):
                opts.append(('i2f', 3))
            kind = self.pick(opts)
            if kind == 'lv':
                lv = self.flvalue(c, False)
                if lv is not None:
                    return lv
                kind = 'const'
            if kind == 'const':
                return self.fconst()
            return self.mk('cast', [self.mk('var', a=self.touch(c, r.choice(ints), False))], t=F32)
        opts = [('arith', 8), ('div', 2 if self.on('fdiv') and self.on('fdivround') else 0),
                ('neg', 2 if self.on('fneg') else 0),
                ('cond', 2 if self.on('fcond') else 0),
                ('call', 3 if self.on('fcall') and self.helpers else 0),
                ('mix', 3 if self.on('mixarith') and c.ints else 0),
                ('i2f', 2 if self.on('i2f') else 0),
                ('asg', 1 if self.on('fasgval') else 0),
                ('incdec', 1 if self.on('fincdecval') and self.on('fincdec') else 0),
                ('chain', 2 if self.on('fchain') else 0)]
        kind = self.pick(opts)
        if kind == 'arith':
            return self.mk('bin', [self.fexpr(c, depth - 1), self.fexpr(c, depth - 1)],
                           r.choice(FARITH))
        if kind == 'div':
            return self.mk('bin', [self.fexpr(c, depth - 1), self.fexpr(c, depth - 1)], '/')
        if kind == 'neg':
            e = self.fexpr(c, depth - 1)
            if is_fconst(e) and not self.on('fconstfold'):
                return e
            if is_blockfloat(e) and not self.on('fblockneg'):
                return e
            return self.mk('un', [e], '-')
        if kind == 'cond':
            test = self.cond(c, depth - 1)
            if not self.on('fconstcond') and base.is_constant(test):
                return self.fexpr(c, depth - 1)     # BUG-052: a folded `?:`
            e = self.mk('cond', [test, self.fexpr(c, depth - 1), self.fexpr(c, depth - 1)])
            if not self.on('fconstcond') and is_paramcond(e):
                # BUG-052: a parameter arm is passed through `+ 0.0f`
                e.k[1:] = [self.mk('bin', [a, E('const', a=0.0, t=F32)], '+')
                           if a.op == 'var' and a.a.storage == 'param' else a
                           for a in e.k[1:]]
            return e
        if kind == 'call':
            return self.call(c, depth, F32)
        if kind == 'mix':
            # one operand is an integer: the usual conversions make it float
            a = self.fexpr(c, depth - 1)
            b = self.iexpr(c, depth - 1)
            k = [a, b] if r.random() < 0.5 else [b, a]
            return self.mk('bin', k, r.choice(FARITH))
        if kind == 'i2f':
            return self.mk('cast', [self.iexpr(c, depth - 1)], t=F32)
        if kind == 'asg':
            lv = self.flvalue(c, True)
            if lv is None:
                return self.fconst()
            return self.mk('asg', [lv, self.rhs_for(lv, self.fexpr(c, depth - 1))], '=')
        if kind == 'incdec':
            lv = self.flvalue(c, True)
            if lv is None:
                return self.fconst()
            return self.mk('incdec', [lv], (r.choice(('++', '--')), r.random() < 0.5))
        # chain: a left-deep sum/product of three or four terms
        e = self.fexpr(c, 0)
        for _ in range(r.randint(2, 3)):
            e = self.mk('bin', [e, self.fexpr(c, 0)], r.choice(FARITH))
        return e

    def iexpr(self, c, depth, t=None):
        """An integer-typed expression, plain enough to be portable."""
        r = self.rng
        if t is None:
            t = self.int_type()
        if depth <= 0 or r.random() < 0.3:
            ints = self.sequenced(c, c.ints, False)
            cands = [d for d in ints if d.t is t] or ints
            if cands and r.random() < 0.7:
                d = self.touch(c, r.choice(cands), False)
                e = self.mk('var', a=d)
                return e if d.t is t else self.mk('cast', [e], t=t)
            return self.iconst(t)
        opts = [('f2i', 5 if self.on('f2i') else 0), ('arith', 3), ('cmp', 2 if self.on('fcmp') else 0),
                ('call', 2 if self.on('fcall') and self.helpers else 0)]
        kind = self.pick(opts)
        if kind == 'f2i':
            if (t.bits == 8 and not self.on('f2i8')) or (not t.signed and not self.on('f2u')):
                return self.iconst(t)
            return self.mk('cast', [self.fexpr(c, depth - 1)], t=t)
        if kind == 'arith':
            # computed in 32 bits so neither 16-bit int can overflow
            a = self.mk('cast', [self.iexpr(c, depth - 1)], t=I32)
            b = self.mk('cast', [self.small(0, 100)], t=I32)
            e = self.mk('bin', [a, b], r.choice(('+', '-')))
            return self.mk('cast', [e], t=t)
        if kind == 'cmp':
            e = self.cond(c, depth - 1)
            return self.mk('cast', [e], t=t) if t is not I16 else e
        e = self.call(c, depth, None)
        return e if ty(e) is t else self.mk('cast', [e], t=t)

    def cond(self, c, depth):
        """A controlling expression (dcc type int)."""
        r = self.rng
        opts = [('fcmp', 6 if self.on('fcmp') else 0),
                ('fcmpconst', 3 if self.on('fcmp') and self.on('fcmpconst') else 0),
                ('ftruth', 1 if self.on('ftruth') else 0),
                ('fnot', 1 if self.on('fnot') else 0),
                ('logic', 2 if self.on('flogic') and depth > 0 else 0),
                ('icmp', 2)]
        kind = self.pick(opts)
        if kind == 'fcmp':
            return self.cmp_ok(self.fexpr(c, depth), self.fexpr(c, depth), r.choice(CMP))
        if kind == 'fcmpconst':
            return self.cmp_ok(self.fexpr(c, depth), self.fconst(), r.choice(CMP))
        if kind in ('ftruth', 'fnot'):
            e = self.fexpr(c, depth)
            if is_fconst(e) and not self.on('fconstfold'):
                return self.mk('bin', [e, self.fconst()], r.choice(CMP))
            return e if kind == 'ftruth' else self.mk('un', [e], '!')
        if kind == 'logic':
            e = self.mk('bin', [self.cond(c, depth - 1), self.cond(c, depth - 1)],
                        r.choice(LOGIC))
            if needs_fconst_fold(e) and not self.on('fconstfold'):
                return e.k[0]
            return e
        a = self.iexpr(c, depth, I32)
        return self.mk('bin', [a, self.mk('cast', [self.small(-50, 50)], t=I32)], r.choice(CMP))

    def cmp_ok(self, a, b, rel):
        """a REL b, swapped when the right operand is the BUG-045 shape."""
        e = self.mk('bin', [a, b], rel)
        if is_negcmp(e) and not self.on('fnegcmp'):
            e = self.mk('bin', [b, a], FLIP[rel])
            if is_negcmp(e):
                e = self.mk('bin', [b, a.k[1]], FLIP[rel])
        return e

    def call(self, c, depth, want):
        r = self.rng
        fns = [f for f in self.helpers if f.ret is want] if want is not None else self.helpers
        fns = [f for f in fns if f is not c.fn] or [f for f in self.helpers if f is not c.fn]
        if not fns:
            return self.fconst() if want is F32 else self.iconst(want or I16)
        fn = r.choice(fns)
        args = []
        for p in fn.params:
            if p.t is F32:
                args.append(self.fexpr(c, depth - 1))
            else:
                args.append(self.iexpr(c, depth - 1, p.t))
        e = self.mk('call', args, a=fn)
        if not self.on('floopcallarg') and is_loopcallarg(e):
            e.k = [self.mk('bin', [a, E('const', a=0.0, t=F32)], '+')
                   if (a.k[0] if a.op == 'asg' else a).op == 'var' and ty(a) is F32 else a
                   for a in e.k]
        return e

    # ---- statements

    def gen_stmt(self, c, depth, block):
        r = self.rng
        c.fresh()
        opts = [('fasg', 10), ('fcasg', 4 if self.on('fcassign') else 0),
                ('iasg', 4), ('fincdec', 2 if self.on('fincdec') else 0),
                ('if', 4 if self.on('fif') and depth > 0 else 0),
                ('for', 3 if self.on('ffor') and depth > 0 and c.loop < 2 else 0),
                ('while', 2 if self.on('fwhile') and depth > 0 and c.loop < 2 else 0),
                ('block', 2 if self.on('fblock') and depth > 0 else 0),
                ('ret', 1 if self.on('fret_early') and c.fn.ret is not None and c.loop == 0
                 and depth < self.depth else 0),
                ('call', 1 if self.on('fcall') and self.helpers else 0)]
        kind = self.pick(opts)
        if kind == 'fasg':
            lv = self.flvalue(c, True)
            if lv is None:
                return
            rhs = self.rhs_for(lv, self.fexpr(c, depth + 1))
            block.stmts.append(S('expr', [self.mk('asg', [lv, rhs], '=')]))
        elif kind == 'fcasg':
            lv = self.flvalue(c, True)
            if lv is None:
                return
            ops = ['+=', '-=', '*=']
            if self.on('fdiv') and self.on('fdivround'):
                ops.append('/=')
            rhs = self.fexpr(c, depth) if r.random() < 0.7 or not c.ints \
                else self.iexpr(c, depth)
            rhs = self.rhs_for(lv, rhs)
            block.stmts.append(S('expr', [self.mk('asg', [lv, rhs], r.choice(ops))]))
        elif kind == 'iasg':
            lv = self.ilvalue(c, True)
            if lv is None:
                return
            t = ty(lv)
            if r.random() < 0.6 and self.on('f2i') and (t.bits > 8 or self.on('f2i8')) \
                    and (t.signed or self.on('f2u')):
                # implicit conversion on the store
                rhs = self.rhs_for(lv, self.fexpr(c, depth))
                if self.on('fcassign') and self.on('icasgf') and r.random() < 0.3:
                    block.stmts.append(S('expr', [self.mk('asg', [lv, rhs], r.choice(('+=', '-=', '*=')))]))
                    return
            else:
                rhs = self.rhs_for(lv, self.iexpr(c, depth, t))
            block.stmts.append(S('expr', [self.mk('asg', [lv, rhs], '=')]))
        elif kind == 'fincdec':
            lv = self.flvalue(c, True)
            if lv is None:
                return
            block.stmts.append(S('expr', [self.mk('incdec', [lv], (r.choice(('++', '--')),
                                                                     r.random() < 0.5))]))
        elif kind == 'if':
            then = Block()
            self.body(c, depth - 1, then, r.randint(1, 3))
            blocks = [then]
            if r.random() < 0.5:
                els = Block()
                self.body(c, depth - 1, els, r.randint(1, 2))
                blocks.append(els)
            c.fresh()
            block.stmts.append(S('if', [self.cond(c, 1)], blocks))
        elif kind == 'for':
            cnt = Decl(c.name('i'), I16, 'block')
            body = Block()
            c.loop += 1
            m = c.mark()
            c.ints.append(cnt)
            c.prot.add(cnt)
            self.body(c, depth - 1, body, r.randint(1, 3))
            c.release(m)
            c.prot.discard(cnt)
            c.loop -= 1
            n = r.randint(1, 8)
            cnt.rng = n - 1
            init = self.mk('asg', [self.mk('var', a=cnt), self.small(0, 0)], '=')
            test = self.mk('bin', [self.mk('var', a=cnt), self.small(n, n)], '<')
            step = self.mk('incdec', [self.mk('var', a=cnt)], ('++', True))
            if r.random() < 0.5:
                cnt.init = self.small(0, 0)
                block.stmts.append(S('for', [None, test, step], [body], cnt))
            else:
                cnt.storage = 'local'
                c.fn.body.decls.append(cnt)
                block.stmts.append(S('for', [init, test, step], [body]))
        elif kind == 'while':
            # while (f < limit) { ...; f += step; } with f fresh and the bound
            # proved by an integer counter
            cnt = Decl(c.name('n'), I16, 'local', init=self.small(1, 6))
            c.fn.body.decls.append(cnt)
            acc = Decl(c.name('w'), F32, 'local', init=self.fconst())
            c.fn.body.decls.append(acc)
            body = Block()
            c.loop += 1
            m = c.mark()
            c.floats.append(acc)
            c.prot.add(cnt)
            self.body(c, depth - 1, body, r.randint(1, 3))
            c.release(m)
            c.prot.discard(cnt)
            c.loop -= 1
            dec = self.mk('asg', [self.mk('var', a=cnt),
                                  self.mk('bin', [self.mk('var', a=cnt), self.small(1, 1)], '-')], '=')
            body.stmts.append(S('expr', [dec]))
            test = self.mk('bin', [self.mk('var', a=cnt), self.small(0, 0)], '>')
            if r.random() < 0.5 and self.on('flogic'):
                test = self.mk('bin', [test, self.mk('bin', [self.mk('var', a=acc), self.fconst()],
                                                     r.choice(('<', '>', '!=')))], '&&')
            block.stmts.append(S('while', [test], [body]))
        elif kind == 'block':
            inner = Block()
            m = c.mark()
            for _ in range(r.randint(1, 2)):
                c.fresh()
                if r.random() < 0.7:
                    d = Decl(c.name('b'), F32, 'block', init=self.fexpr(c, 1))
                    c.floats.append(d)
                else:
                    t = self.int_type()
                    init = self.iexpr(c, 1, t)
                    d = Decl(c.name('b'), t, 'block', init=init)
                    c.ints.append(d)
                    if not self.on('finittype') and not init_keeps_type(init, t):
                        # BUG-016: initialise with a constant, assign after
                        d.init = mk_const(0, t if t.bits >= 16 else I16)
                        inner.stmts.append(S('expr', [self.mk('asg', [self.mk('var', a=d), init], '=')]))
                inner.decls.append(d)
            self.body(c, depth - 1, inner, r.randint(1, 3))
            c.release(m)
            block.stmts.append(S('block', [], [inner]))
        elif kind == 'ret':
            cond = self.cond(c, 1)
            for _ in range(8):
                if self.on('fconstjump') or not base.is_constant(cond):
                    break
                c.fresh()
                cond = self.cond(c, 1)
            else:
                return      # BUG-019: `if (K) { return …; }` before a declaring block
            c.fresh()
            then = Block()
            then.stmts.append(S('ret', [self.result(c)]))
            block.stmts.append(S('if', [cond], [then]))
        elif kind == 'call':
            lv = self.flvalue(c, True)
            e = self.call(c, 2, F32)
            if lv is not None:
                e = self.mk('asg', [lv, e], '=')
            block.stmts.append(S('expr', [e]))

    def rhs_for(self, lv, rhs):
        """rhs, unless it has a side effect on lv's object: that is
        unsequenced against the store."""
        if not has_effect(rhs):
            return rhs
        base_e = lv
        while base_e.op in ('fld', 'deref'):
            base_e = base_e.k[0]
        decl = base_e.a if base_e.op in ('var', 'idx') else None
        if decl is None or mentions(rhs, decl) or (lv.op == 'deref'):
            return self.fconst() if ty(rhs) is F32 else self.iconst(ty(rhs))
        return rhs

    def body(self, c, depth, block, n):
        for _ in range(n):
            self.gen_stmt(c, depth, block)

    def result(self, c):
        t = c.fn.ret
        c.fresh()
        e = self.fexpr(c, 2) if t is F32 else self.iexpr(c, 2, t)
        if is_retconv(c.fn, e) and not self.on('fretconv'):
            return self.mk('cast', [e], t=t)
        return e

    # ---- functions

    def params_for(self, c, fn, n):
        r = self.rng
        for i in range(n):
            if r.random() < 0.65 and self.on('fparam') or not self.on('iparam'):
                d = Decl('x%d' % i, F32, 'param')
                c.floats.append(d)
            else:
                t = self.int_type()
                d = Decl('x%d' % i, t, 'param')
                c.ints.append(d)
            fn.params.append(d)

    def locals_for(self, c, fn, n):
        r = self.rng
        for i in range(n):
            x = r.random()
            if x < 0.55:
                d = Decl('l%d' % i, F32, 'local', init=self.fvalue())
                c.floats.append(d)
            elif x < 0.65 and self.on('fptr') and (c.floats or c.farrs):
                tgts = c.floats + c.farrs
                if not self.on('fptrparam'):
                    tgts = [t for t in tgts if t.storage != 'param'] or c.farrs
                if not tgts:
                    d = Decl('l%d' % i, F32, 'local', init=self.fvalue())
                    c.floats.append(d)
                    continue
                tgt = r.choice(tgts)
                if tgt.dims:
                    k = r.randint(0, tgt.dims[0] - 1)
                    init = self.mk('addr', [self.small(k, k)], a=(tgt, 1))
                else:
                    init = self.mk('addrof', a=tgt)
                    tgt.addr_taken = True
                d = Decl('p%d' % i, PtrType(F32), 'local', init=init)
                d.tgt_local = tgt.storage != 'global'
                c.fptrs.append(d)
            elif x < 0.75 and self.on('farr'):
                d = Decl('la%d' % i, F32, 'local', dims=(4,), init=self.init_value(F32, (4,)))
                c.farrs.append(d)
            else:
                t = self.int_type()
                # an initialiser of the local's own type (BUG-016 is logged)
                d = Decl('l%d' % i, t, 'local', init=self.ivalue(t))
                c.ints.append(d)
            fn.body.decls.append(d)

    def ret_type(self):
        return F32 if self.rng.random() < 0.7 or not self.on('fret') else self.int_type()

    def gen_helper(self, n):
        r = self.rng
        fn = Func('h%d' % n, self.ret_type(), [], 'pure')
        c = self.ctx(fn, True)
        self.params_for(c, fn, r.randint(1, 3))
        self.locals_for(c, fn, r.randint(0, 2))
        for _ in range(r.randint(0, 3)):
            self.gen_stmt(c, 1, fn.body)
        fn.body.stmts.append(S('ret', [self.result(c)]))
        return fn

    def gen_test(self, n):
        r = self.rng
        fn = Func('f%d' % n, self.ret_type(), [], 'test')
        fn.static = r.random() < 0.7
        c = self.ctx(fn, False)
        self.params_for(c, fn, r.randint(0, 4))
        self.locals_for(c, fn, r.randint(2, 5))
        for _ in range(r.randint(max(2, self.size // 2), self.size)):
            self.gen_stmt(c, self.depth, fn.body)
        fn.body.stmts.append(S('ret', [self.result(c)]))
        for _ in range(3):
            args = []
            for p in fn.params:
                if p.t is F32:
                    args.append(self.mk('const', a=self.fvalue(), t=F32))
                else:
                    t = p.t if p.t.bits >= 16 else (I16 if p.t.signed else U16)
                    args.append(self.mk('const', a=t.wrap(self.ivalue(p.t)), t=t))
            fn.inputs.append(args)
        return fn

    def generate(self):
        r = self.rng
        self.gen_structs()
        self.gen_globals()
        for n in range(r.randint(2, 3)):
            fn = self.gen_helper(n)
            self.prog.funcs.append(fn)
            self.helpers.append(fn)
        for n in range(self.n_tests):
            self.prog.funcs.append(self.gen_test(n))
        self.steer()
        return self.prog

    # ---- post-generation steering away from logged shapes

    def fcast(self, e):
        return self.mk('cast', [e], t=F32)

    def fix_negrhs(self, rhs):
        """`C OP y` rewritten so the constant is not the left operand,
        exactly: `y + C`, `y * C`, `-(y - C)`, `(C / y) + 0.0f`. rhs is
        edited in place so the slot walk still visits its operands."""
        c, y = rhs.k
        if rhs.a in ('+', '*'):
            rhs.k[0], rhs.k[1] = y, c
            return rhs
        if rhs.a == '-':
            rhs.k[0], rhs.k[1] = y, c
            return self.mk('un', [rhs], '-')
        return self.mk('bin', [rhs, E('const', a=0.0, t=F32)], '+')

    def strip_u32s(self, e):
        """The BUG-048/050 cast chain e with the offending cast removed:
        `(uint32_t)x` -> `x`; `(int32_t)((uint32_t)n + 1)` -> `(int32_t)(n + 1)`;
        `(int32_t)(u32 expression)` -> `(int32_t)(uint16_t)(u32 expression)`."""
        if e.t is U32:
            return e.k[0]
        outer = e
        while outer.k[0].op == 'cast' and outer.k[0].t is I32:
            outer = outer.k[0]
        inner = outer.k[0]
        if ty(inner) is U32:
            outer.k[0] = self.mk('cast', [inner], t=U16)
            return e
        if inner.op == 'cast':
            e.k[0] = inner.k[0]
        else:
            while inner.k[0].op != 'cast':
                inner = inner.k[0]
            inner.k[0] = inner.k[0].k[0]
        return e

    def steer(self):
        """Rewrite the shapes of KNOWN_BUGS that are easier to recognise in
        the finished tree than at the point of generation. Each rewrite
        keeps the program valid C with the same meaning under clang. A
        rewrite can expose another shape, so the pass runs to a fixpoint."""
        for _ in range(4):
            before = self.prog.nid
            self.steer_pass()
            if self.prog.nid == before:
                break

    def steer_pass(self):
        for fn in self.prog.funcs:
            paramcmp = not self.on('fparamcmp')
            for slot in expr_slots(self.prog, [fn]):
                e = slot_get(slot)
                # the child lists are edited in place so that the slots
                # collected above stay valid
                if paramcmp and is_paramcmp(e):
                    # `v REL (p OP x)` -> `(p OP x) FLIP v`, else `v REL ((p OP x) + 0)`
                    self.prog.nid += 1
                    swapped = self.mk('bin', [e.k[1], e.k[0]], FLIP[e.a])
                    if not is_anyconst(e.k[0]) and not is_paramcmp(swapped):
                        e.k[0], e.k[1] = e.k[1], e.k[0]
                        e.a = FLIP[e.a]
                    else:
                        rhs = e.k[1]
                        zero = E('const', a=0.0, t=F32) if ty(rhs) is F32 \
                            else mk_const(0, ty(rhs))
                        e.k[1] = self.mk('bin', [rhs, zero], '+')
                if not self.on('fnegcmp') and is_negcmp(e):
                    self.prog.nid += 1
                    if is_anyconst(e.k[1]):
                        # `x REL (C1 OP C2)` -> `(C1 OP C2) FLIP x`
                        e.k[0], e.k[1] = e.k[1], e.k[0]
                        e.a = FLIP[e.a]
                    elif e.k[1].k[0].op == 'const':
                        e.k[1] = self.fix_negrhs(e.k[1])
                    else:
                        # `x REL ((C) OP y)` -> `x REL (((C) OP y) * 1.0f)`
                        e.k[1] = self.mk('bin', [e.k[1], E('const', a=1.0, t=F32)], '*')
                if not self.on('fintprodadd') and is_intprodadd(e):
                    if e.op == 'bin' and not is_fprod(e.k[0]):
                        self.prog.nid += 1
                        e.k[0], e.k[1] = e.k[1], e.k[0]      # `(x * y) + f`
                    else:                                    # `f + ((x * y) + 0.0f)`
                        e.k[1] = self.mk('bin', [e.k[1], E('const', a=0.0, t=F32)], '+')
                if not self.on('fnegzero_not') and e.op == 'un' and e.a == '!' \
                        and ty(e.k[0]) is F32:
                    e2 = self.mk('bin', [], '==')
                    e2.k = e.k
                    e2.k.append(E('const', a=0.0, t=F32))
                    slot_set(slot, e2)
                    e = e2
                if not self.on('fblockint'):
                    for i in blockint_operands(e):
                        e.k[i] = self.fcast(e.k[i])
                if not self.on('fselfop') and is_selfop(e):
                    # `b OP b` -> `b OP (b + 0)`
                    t = ty(e.k[1])
                    zero = E('const', a=0.0, t=F32) if t is F32 else mk_const(0, t)
                    e.k[1] = self.mk('bin', [e.k[1], zero], '+')
                if not self.on('fu32cast'):
                    for i, (k, want) in enumerate(zip(e.k, float_contexts(e))):
                        if want is F32 and is_u32s(k):
                            self.prog.nid += 1
                            e.k[i] = self.strip_u32s(k)
            if not self.on('fu32cast'):
                for b in all_blocks_of(fn):
                    for d in b.decls:
                        if d.t is F32 and isinstance(d.init, E) and is_u32s(d.init):
                            self.prog.nid += 1
                            d.init = self.strip_u32s(d.init)


def all_blocks_of(fn):
    out = []

    def walk(b):
        out.append(b)
        for st in b.stmts:
            for sub in st.b:
                if sub is not None:
                    walk(sub)
    walk(fn.body)
    return out


def generate(seed, tests=6, size=10, depth=3, off=frozenset()):
    return Gen(seed, tests, size, depth, off).generate()


def render(prog, checked=False, title=''):
    return FRenderer(checked, prog.off).program(prog, title)
