#!/usr/bin/env python3
"""Program model, generator and renderers for clang-oracle-fuzz.py.

A generated program is an AST (structs, globals, helper functions, test
functions, a fixed harness) that is rendered two ways:

  natural   the C source that both clang and dcc compile. It uses fixed-width
            types only, so `int` is never spelled and host/target differ only
            in the width integer promotion uses (32 bits against 16).
  checked   the same program for the host only, with every operation that
            could be undefined, or could differ at 16-bit `int`, routed
            through a ck_* helper that records the node's id when it trips.

The renderer, not the generator, owns portability: for any well-typed tree it
inserts the casts and masks that make the natural source mean the same thing
with 16-bit and 32-bit `int`:

  * a result whose dcc type is `unsigned int` is cast to uint16_t, because the
    host computes it exactly in `int` and does not wrap;
  * `/`, `%` and comparisons whose dcc type is `unsigned int` get their signed
    operand cast to uint16_t, because the host would compare signed values;
  * array indexes and shift counts are masked into range;
  * constants are spelled with casts, never with `L`/`U` suffixes, because
    `long` is 64 bits on the host.

What the renderer cannot make portable is undefined behaviour at 16 bits that
is defined at 32 (signed overflow of `int`, shifting into the sign bit) and
ordinary undefined behaviour (division by zero, 32-bit signed overflow). The
checked rendering detects those at run time; the driver then repairs the
tripped nodes or rejects the program. A program is used only when its checked
build reports no trips, so its natural source is defined for both compilers.
"""
from __future__ import annotations

import random
import struct

CMP = ('<', '>', '<=', '>=', '==', '!=')
LOGIC = ('&&', '||')
ARITH = ('+', '-', '*')
BITS = ('&', '|', '^')
DIV = ('/', '%')
SHIFT = ('<<', '>>')


class IntType:
    __slots__ = ('name', 'bits', 'signed', 'cnames')

    def __init__(self, name, bits, signed, cnames):
        self.name, self.bits, self.signed, self.cnames = name, bits, signed, cnames

    def __deepcopy__(self, memo):
        return self

    def __copy__(self):
        return self

    @property
    def cname(self):
        return self.cnames[0]

    @property
    def lo(self):
        return -(1 << (self.bits - 1)) if self.signed else 0

    @property
    def hi(self):
        return (1 << (self.bits - 1)) - 1 if self.signed else (1 << self.bits) - 1

    def wrap(self, v):
        v &= (1 << self.bits) - 1
        if self.signed and v >> (self.bits - 1):
            v -= 1 << self.bits
        return v

    def __repr__(self):
        return self.name


I8 = IntType('i8', 8, True, ('int8_t', 'signed char', 'char'))
U8 = IntType('u8', 8, False, ('uint8_t', 'unsigned char'))
I16 = IntType('i16', 16, True, ('int16_t', 'short'))
U16 = IntType('u16', 16, False, ('uint16_t', 'unsigned short'))
I32 = IntType('i32', 32, True, ('int32_t',))
U32 = IntType('u32', 32, False, ('uint32_t',))
INT_TYPES = (I8, U8, I16, U16, I32, U32)
UNSIGNED_OF = {I8: U8, I16: U16, I32: U32, U8: U8, U16: U16, U32: U32}


class FloatType:
    """dcc's only floating type: 32-bit IEEE single. Only clang_oracle_float_gen
    creates values of it; integer programs never contain one."""
    __slots__ = ('name', 'bits', 'signed', 'cnames')

    def __init__(self):
        self.name, self.bits, self.signed, self.cnames = 'f32', 32, True, ('float',)

    def __deepcopy__(self, memo):
        return self

    def __copy__(self):
        return self

    @property
    def cname(self):
        return 'float'

    def wrap(self, v):
        return struct.unpack('f', struct.pack('f', float(v)))[0]

    def __repr__(self):
        return self.name


F32 = FloatType()


def is_float(t):
    return t is F32


def is_scalar(t):
    return isinstance(t, IntType) or t is F32


def float_text(v):
    """A float constant: shortest decimal that round-trips to the single v."""
    v = F32.wrap(v)
    if v == 0:
        return '0.0f'
    for digits in range(6, 10):
        s = '%.*g' % (digits, v)
        if F32.wrap(float(s)) == v:
            break
    if 'e' not in s and '.' not in s:
        s += '.0'
    s += 'f'
    return '(%s)' % s if v < 0 else s


class PtrType:
    __slots__ = ('to',)

    def __init__(self, to):
        self.to = to


class Field:
    __slots__ = ('name', 't', 'dims', 'bf')

    def __init__(self, name, t, dims=(), bf=0):
        self.name, self.t, self.dims, self.bf = name, t, dims, bf


class StructType:
    __slots__ = ('name', 'fields')

    def __init__(self, name, fields):
        self.name, self.fields = name, fields

    @property
    def cname(self):
        return 'struct ' + self.name


def is_int(t):
    return isinstance(t, IntType)


def promote(t):
    """dcc's integer promotion: everything narrower than int becomes int."""
    if t.bits < 16:
        return I16
    return t


def common(a, b):
    """dcc's usual arithmetic conversions (int 16 bits, long 32 bits)."""
    if a is F32 or b is F32:
        return F32
    a, b = promote(a), promote(b)
    if a.bits == 32 or b.bits == 32:
        return U32 if U32 in (a, b) else I32
    return U16 if U16 in (a, b) else I16


class Decl:
    """A variable. storage: global, param, local (function top), block."""
    __slots__ = ('name', 't', 'dims', 'storage', 'init', 'cname', 'addr_taken',
                 'valid', 'tgt_local', 'rng', 'static')

    def __init__(self, name, t, storage, dims=(), init=None, cname=None):
        self.name, self.t, self.storage, self.dims, self.init = name, t, storage, dims, init
        self.cname = cname or (t.cname if not isinstance(t, PtrType) else None)
        self.addr_taken = False
        self.valid = 1          # pointers: elements valid from the pointee
        self.tgt_local = False  # pointers: pointee is local to the function
        self.rng = None         # loop counters: largest value seen in the body
        self.static = False


class E:
    """Expression node.

    op      k (children)            a
    const   -                       value            (t stored)
    var     -                       Decl
    idx     one index per dim       array Decl
    fld     base [, index]          Field
    deref   pointer expression      -
    pidx    index                   pointer Decl
    addr    index                   (array Decl, elements needed)
    aend    -                       array Decl       one past the end
    addrof  -                       Decl
    un      operand                 operator
    bin     left, right             operator
    cast    operand                 -                (t stored)
    cond    test, then, else        -
    call    arguments               Func
    asg     lvalue, value           operator (= += ...)
    incdec  lvalue                  (operator, is_prefix)
    """
    __slots__ = ('op', 't', 'k', 'a', 'id')

    def __init__(self, op, k=(), a=None, t=None, id=0):
        self.op, self.k, self.a, self.t, self.id = op, list(k), a, t, id


class S:
    """Statement node.

    op        e (expressions)       b (blocks)        a
    expr      expression
    if        test                  then [, else]
    for       init, test, step      body              Decl for a C99 init decl
    while     test                  body
    do        test                  body
    switch    control               one per case      [(labels|None, has_break)]
    break / continue
    ret       [value]
    block                           body
    goto      test                                    label
    label                                             label
    """
    __slots__ = ('op', 'e', 'b', 'a')

    def __init__(self, op, e=(), b=(), a=None):
        self.op, self.e, self.b, self.a = op, list(e), list(b), a


class Block:
    __slots__ = ('decls', 'stmts')

    def __init__(self):
        self.decls, self.stmts = [], []


class Func:
    __slots__ = ('name', 'ret', 'params', 'body', 'kind', 'static', 'inputs', 'via_ptr')

    def __init__(self, name, ret, params, kind):
        self.name, self.ret, self.params, self.kind = name, ret, params, kind
        self.body = Block()
        self.static = True
        self.inputs = []
        self.via_ptr = False


class Program:
    __slots__ = ('seed', 'structs', 'globals', 'funcs', 'nid', 'off', 'floats')

    def __init__(self, seed, off=frozenset()):
        self.seed = seed
        self.off = frozenset(off)   # generator features left out; the renderer honours some
        self.structs, self.globals, self.funcs = [], [], []
        self.nid = 1
        self.floats = False         # uses F32: emit the float harness and checks

    @property
    def tests(self):
        return [f for f in self.funcs if f.kind == 'test']


def ty(e):
    """dcc type of an expression."""
    op = e.op
    if op in ('const', 'cast'):
        return e.t
    if op in ('var', 'idx'):
        return e.a.t
    if op == 'fld':
        return I16 if e.a.bf else e.a.t
    if op == 'deref':
        return ty(e.k[0]).to
    if op == 'pidx':
        return e.a.t.to
    if op == 'addr':
        return PtrType(e.a[0].t)
    if op in ('aend', 'addrof'):
        return PtrType(e.a.t)
    if op == 'un':
        return I16 if e.a == '!' else promote(ty(e.k[0]))
    if op == 'bin':
        if e.a in CMP or e.a in LOGIC:
            return I16
        ta = ty(e.k[0])
        if e.a in SHIFT:
            return promote(ta)
        return common(ta, ty(e.k[1]))
    if op == 'cond':
        ta = ty(e.k[1])
        return common(ta, ty(e.k[2])) if is_int(ta) else ta
    if op == 'call':
        return e.a.ret
    if op == 'fclamp':
        return F32
    if op in ('asg', 'incdec'):
        return ty(e.k[0])
    raise ValueError(op)


def clone(e):
    """Copy an expression tree, sharing the declarations it refers to."""
    return E(e.op, [clone(k) for k in e.k], e.a, e.t, e.id)


def is_bitfield(e):
    return e.op == 'fld' and e.a.bf


# ---------------------------------------------------------------- traversal

def block_slots(block, out):
    for d in block.decls:
        if isinstance(d.init, E):
            out.append((d, 'init'))
    for s in block.stmts:
        stmt_slots(s, out)


def stmt_slots(s, out):
    if s.op == 'for' and s.a is not None and isinstance(s.a.init, E):
        out.append((s.a, 'init'))
    for i, e in enumerate(s.e):
        if e is not None:
            out.append((s.e, i))
    for b in s.b:
        if b is not None:
            block_slots(b, out)


def expr_slots(prog, funcs=None):
    """Every (container, key) holding a root expression, then all sub-slots."""
    roots = []
    for f in (funcs if funcs is not None else prog.funcs):
        block_slots(f.body, roots)
    out = []
    stack = list(reversed(roots))
    while stack:
        slot = stack.pop()
        out.append(slot)
        e = slot_get(slot)
        for i in range(len(e.k) - 1, -1, -1):
            stack.append((e.k, i))
    return out


def slot_get(slot):
    c, k = slot
    return getattr(c, k) if isinstance(k, str) else c[k]


def slot_set(slot, v):
    c, k = slot
    if isinstance(k, str):
        setattr(c, k, v)
    else:
        c[k] = v


def all_blocks(prog):
    out = []

    def walk(b):
        out.append(b)
        for s in b.stmts:
            for sub in s.b:
                if sub is not None:
                    walk(sub)

    for f in prog.funcs:
        walk(f.body)
    return out


def init_keeps_type(init, t):
    """init, as the initialiser of a local of integer type t, has exactly
    that type already: a constant in t's range, or an expression of type t
    that is not a cast from another type. Anything else is the BUG-016 shape,
    where the local takes on the initialiser's signedness."""
    if init.op == 'const':
        return t.lo <= init.a <= t.hi
    if init.op == 'cast':
        return ty(init.k[0]) is t
    return ty(init) is t


def folds(e):
    """e is a constant once an assignment of a constant is taken as its
    value: `~3`, `-(x = 77)`."""
    if e.op == 'asg':
        return e.a == '=' and folds(e.k[1])
    return e.op == 'const' or (e.op in ('bin', 'un', 'cast', 'cond')
                               and all(folds(k) for k in e.k))


def negative_constant(rhs, t):
    """rhs is a constant, possibly under casts, that is negative before or
    after them or, for a signed 32-bit target, has the top bit set: the
    BUG-008 shape. Any other expression that folds is taken to be one."""
    casts = []
    while rhs.op == 'cast':
        casts.append(ty(rhs))
        rhs = rhs.k[0]
    if rhs.op != 'const':
        return folds(rhs)
    v = rhs.a
    for ct in reversed(casts):
        v = ct.wrap(v) if is_int(ct) else v
    return rhs.a < 0 or v < 0 or (t is I32 and v >= 1 << 31)


def same_variable(a, b):
    """a and b are the same variable: as the two operands of one binary
    operator, the BUG-026 shape."""
    return a.op == 'var' and b.op == 'var' and a.a is b.a


def peep_byte_self(op, operands):
    """Detect byte binary self-use for optional coverage steering."""
    return op not in (*LOGIC, '=') and same_variable(*operands) \
        and is_int(ty(operands[0])) and ty(operands[0]).bits == 8


def nested_if_else(s):
    """s is an `if` with, directly in one of its arms, an `if` that has an
    `else`: the BUG-027 shape."""
    return s.op == 'if' and any(
        t.op == 'if' and len(t.b) > 1 and t.b[1] is not None
        for arm in s.b if arm is not None for t in arm.stmts)


def byte_arm(s):
    """s is an `if … else` with a plain assignment to an 8-bit local or
    parameter directly in one of its arms: the BUG-029 shape when the other
    arm assigns another variable."""
    if s.op != 'if' or len(s.b) < 2 or s.b[1] is None:
        return False
    for arm in s.b:
        for t in arm.stmts:
            e = t.e[0] if t.op == 'expr' and t.e else None
            if e is not None and e.op == 'asg' and e.a == '=' and e.k[0].op == 'var' \
                    and e.k[0].a.storage != 'global' and is_int(e.k[0].a.t) \
                    and e.k[0].a.t.bits == 8:
                return True
    return False


def word_arm(s):
    """s is an `if … else` with a plain assignment to a 16-bit local directly
    in one of its arms, or as the condition of an `if` there: the BUG-038
    shape when the local is updated and an early return follows."""
    if s.op != 'if' or len(s.b) < 2 or s.b[1] is None:
        return False
    for arm in s.b:
        for t in arm.stmts:
            e = t.e[0] if t.e and t.op in ('expr', 'if') else None
            if e is not None and e.op == 'asg' and e.a == '=' and e.k[0].op == 'var' \
                    and e.k[0].a.storage != 'global' and is_int(e.k[0].a.t) \
                    and e.k[0].a.t.bits == 16:
                return True
    return False


def block_return(ret, e):
    """`return e;` to return type ret, where e is a nested-block object that
    needs converting: the BUG-025 shape."""
    return block_object(e) and (ret.bits == 8 or (ret.bits == 32 and ty(e).bits < 32))


def wide_block_copy(d):
    """d is a 32-bit nested-block local initialised from a local or a
    parameter by name: the BUG-031 shape."""
    return d.storage == 'block' and is_int(d.t) and d.t.bits == 32 \
        and isinstance(d.init, E) and d.init.op == 'var' and d.init.a.storage != 'global'


def wide_block_store(lv, rhs):
    """A 32-bit nested-block local stored as it stands to a 16-bit element,
    pointee or member reached through a pointer: the BUG-031 shape when the
    address is computed (seed 20149 stores 0 for a constant-initialised
    local through a variable index)."""
    return lv.op != 'var' and is_int(ty(lv)) and ty(lv).bits == 16 \
        and rhs.op == 'var' and rhs.a.storage == 'block' and is_int(rhs.a.t) \
        and rhs.a.t.bits == 32


def const_div_zero(e):
    """e is a constant divided by a constant zero, which dcc rejects even
    where it is never evaluated: the BUG-033 shape."""
    return e.op == 'bin' and e.a in DIV and e.k[1].op == 'const' and e.k[1].a == 0 \
        and is_constant(e.k[0])


def block_object(e):
    """e names, or assigns to, an object declared in a nested block: as the
    operand of `return` in a function returning an 8-bit type, the BUG-025
    shape."""
    if e.op in ('incdec', 'asg'):
        e = e.k[0]
    return e.op in ('var', 'idx') and e.a.storage == 'block'


def cross_type(rhs, t):
    """rhs, stored to a 16-bit local of type t, arrives as a value of another
    type, or through a cast that only changes signedness: the BUG-023 shape.
    A constant, and a cast that also changes width, `(t)(int32_t)e`, are
    not."""
    if is_constant(rhs):
        return False
    if ty(rhs) is not t:
        return True
    if rhs.op == 'cast':
        inner = ty(rhs.k[0])
        return is_int(inner) and inner is not t and inner.bits == 16
    return False


def logic_value(e):
    """e is `!x`, `x && y` or `x || y`: under unary `-` or `~` with constant
    operands, the BUG-012 shape."""
    return (e.op == 'un' and e.a == '!') or (e.op == 'bin' and e.a in LOGIC)


def is_constant(e):
    """e names no object and calls nothing, so a compiler can fold it."""
    return e.op == 'const' or (e.op in ('bin', 'un', 'cast', 'cond')
                               and all(is_constant(k) for k in e.k))


def const_jump(s):
    """s jumps on a condition that folds, leaving what follows it
    unreachable: with a declaring scope after it, the BUG-019 shape."""
    if s.op == 'goto':
        return s.e[0] is not None and is_constant(s.e[0])
    return s.op == 'if' and is_constant(s.e[0]) and any(
        x.op in ('ret', 'break', 'continue') for sub in s.b if sub is not None
        for x in sub.stmts)


def has_block_wide(e):
    if e.op == 'var':
        return e.a.storage == 'block' and is_int(e.a.t) and e.a.t.bits == 32
    return any(has_block_wide(k) for k in e.k)


def known_shapes(prog):
    """The features in prog.off whose bug shape the program contains anyway.

    The generator steers round a logged bug when its feature is off, but the
    reducer edits trees freely and could shrink a new failure into an old one.
    It uses this to refuse an edit that introduces a shape the program did not
    already have."""
    found = set()
    # a loop's own counter step, `for (…; …; i++)` or `while (w++ < n)`, is in
    # every program and is not one of the logged increment shapes
    steps = set()
    for b in all_blocks(prog):
        for s in b.stmts:
            if s.op == 'for' and s.e[2] is not None:
                steps.add(id(s.e[2]))
            elif s.op == 'while' and s.e[0].op == 'bin' and s.e[0].k[0].op == 'incdec':
                steps.add(id(s.e[0].k[0]))

    def stored(rhs, d):
        if not is_int(d.t) or d.storage == 'global':
            return
        if (d.t.bits <= 16 or d.storage == 'block') and rhs.op == 'bin' and rhs.a in LOGIC:
            found.add('logicinit')
        if d.storage == 'block' and d.t.bits == 32 and negative_constant(rhs, d.t):
            found.add('blockwideneg')

    def expr(e, root):
        op = e.op
        if op in ('bin', 'asg') and peep_byte_self(e.a, e.k):
            found.add('peepbyteself')
        if op == 'asg':
            lv, rhs = e.k
            lt = ty(lv)
            if is_int(lt) and e.a == '=':
                if lv.op == 'var':
                    stored(rhs, lv.a)
                    if lv.a.storage == 'block' and lt.bits == 32 and rhs.op == 'var' \
                            and rhs.a.storage != 'global':
                        found.add('blockwidecopy')
                    if lv.a.storage != 'global' and lt.bits == 32 and rhs.op == 'var' \
                            and rhs.a.storage != 'global' and is_int(rhs.a.t) \
                            and rhs.a.t.bits == 32:
                        found.add('loopcopy')
                    if lv.a.storage != 'global' and lt.bits == 16 and cross_type(rhs, lt):
                        found.add('crosstypeasg')
                if wide_block_store(lv, rhs):
                    found.add('blockwidecopy')
            elif is_int(lt):
                local = lv.op == 'var' and lv.a.storage != 'global'
                if lv.op != 'var' and lt.bits < 32 and ty(rhs).bits == 32:
                    found.add('widecassign')
                if ty(rhs).bits == 32 and (is_bitfield(lv) or (
                        lv.op == 'fld' and lv.a.dims and lt.bits == 16)):
                    found.add('widecasgbf')
                if e.a == '<<=' and lt.bits == 8 and ty(rhs).bits == 8:
                    found.add('bytevarshl')
                if e.a == '>>=' and lt is I8 and ty(rhs) is U16:
                    found.add('bytesarcount')
                if e.a in ('/=', '%=', '>>=') and is_bitfield(lv) and not lv.a.t.signed:
                    found.add('bfdivasg')
                if e.a in ('/=', '%=') and lv.op == 'fld' and lt.bits == 32:
                    found.add('blockwidediv')
                if lv.op == 'fld' and lv.a.dims and lt.bits == 8:
                    found.add('structbytecasg')
                if lv.op == 'var' and rhs.op == 'var' and rhs.a is lv.a:
                    found.add('selfand')
                if local and lt.bits == 32 and e.a == '+=' and rhs.op == 'const' and rhs.a == 1 \
                        and id(e) not in steps:
                    found.add('widelocalinc')
                if local and lt.bits == 16 and e.a == '+=' and rhs.op == 'const' \
                        and 2 <= rhs.a <= 255:
                    found.add('peepsum')
        elif op == 'incdec':
            lv = e.k[0]
            lt = ty(lv)
            if lv.op == 'var' and lv.a.storage == 'block' and lt is U16:
                found.add('blockdec')
            local = lv.op == 'var' and lv.a.storage != 'global'
            if is_int(lt) and lt.bits == 32:
                if lv.op in ('idx', 'pidx') or (lv.op == 'fld' and lv.a.dims):
                    found.add('wideidxincdec')
                if id(e) not in steps:
                    if lv.op == 'var' and not e.a[1] and not root:
                        found.add('widepostinc')
                    if local and e.a[0] == '++':
                        found.add('widelocalinc')
            if is_int(lt) and lt.bits == 8 and lv.op == 'var' and not e.a[1] and not root \
                    and id(e) not in steps:
                found.add('postincvalue')
        elif op == 'un':
            if e.a in ('-', '~') and (logic_value(e.k[0]) or (
                    e.k[0].op != 'const' and is_constant(e.k[0]))):
                found.add('negnotconst')
        elif op == 'bin':
            if same_variable(e.k[0], e.k[1]):
                found.add('selfoperand')
            for i in (0, 1):
                c, o = e.k[i], e.k[1 - i]
                if c.op == 'cast' and c.t is I16 and ty(c.k[0]) is U16 and has_block_wide(o):
                    found.add('blockcast')
            if const_div_zero(e):
                found.add('constdivzero')
        elif op == 'call':
            if any(a.op == 'var' and a.a.storage != 'global' and is_int(a.a.t)
                   and a.a.t.bits == 32 for a in e.k):
                found.add('loopcallarg')
            if e.a.via_ptr and any(is_int(p.t) and ty(a) is not p.t
                                   for a, p in zip(e.k, e.a.params)):
                found.add('ptrcallconv')
        for k in e.k:
            expr(k, False)

    def decl(d):
        if wide_block_copy(d):
            found.add('blockwidecopy')
        if d.storage == 'block' and isinstance(d.t, PtrType) and d.t.to is U16:
            found.add('blockptr')
        if isinstance(d.init, E):
            if is_int(d.t):
                stored(d.init, d)
                if not init_keeps_type(d.init, d.t):
                    found.add('inittype')
            expr(d.init, True)

    for b in all_blocks(prog):
        for d in b.decls:
            decl(d)
        for s in b.stmts:
            if const_jump(s):
                found.add('constjump')
            if nested_if_else(s):
                found.add('nestedifelse')
            if byte_arm(s):
                found.add('bytearm')
            if word_arm(s):
                found.add('wordarm')
            if s.op == 'for' and s.a is not None:
                decl(s.a)
            for e in s.e:
                if e is not None:
                    expr(e, True)

    def returns(f, b):
        for s in b.stmts:
            if s.op == 'ret' and s.e and block_return(f.ret, s.e[0]):
                found.add('blockret')
            for sub in s.b:
                if sub is not None:
                    returns(f, sub)

    def arms(b, inside):
        """A `for` that declares its counter, anywhere inside another
        statement: the BUG-030 shape when an earlier sibling scope declares
        a local too."""
        for s in b.stmts:
            if inside and s.op == 'for' and s.a is not None:
                found.add('armfordecl')
            for sub in s.b:
                if sub is not None:
                    arms(sub, True)

    for f in prog.funcs:
        arms(f.body, False)
        if f.ret is not None and is_int(f.ret):
            returns(f, f.body)
    return found & prog.off


# ------------------------------------------------------------------- repair

def mk_const(v, t):
    return E('const', a=t.wrap(v), t=t)


def simple_const(v, t):
    """A constant of type t worth v (the reducer's replacement value)."""
    ct = t if t.bits >= 16 else (I16 if t.signed else U16)
    return mk_const(v, ct)


def wrap_unsigned(node):
    """Rewrite a signed bin/un node in place as the same operation carried out
    in the unsigned type of its width, converted back to the signed type."""
    t = ty(node)
    u = UNSIGNED_OF[t]
    kids = [E('cast', [k], t=u) for k in node.k]
    if node.op == 'bin' and node.a in SHIFT:
        kids[1] = node.k[1]
    inner = E(node.op, kids, node.a, id=node.id)
    node.op, node.k, node.a, node.t = 'cast', [inner], None, t


def safe_divisor(node):
    b = node.k[1]
    masked = E('bin', [b, mk_const(63, I16)], '&')
    node.k[1] = E('bin', [masked, mk_const(1, I16)], '+')


def repair_node(node):
    """Make a tripped node defined. Returns False when it cannot."""
    op = node.op
    if op == 'bin':
        if node.a in DIV:
            if node.k[1].op == 'bin' and node.k[1].a == '+' and node.k[1].k[1].op == 'const':
                node.k[1] = mk_const(3, I16)
            else:
                safe_divisor(node)
            return True
        if node.a == '*' and not ty(node).signed:
            node.k[1] = E('bin', [node.k[1], mk_const(0x7f, I16)], '&')
            return True
        if node.a in ARITH or node.a == '<<':
            if not ty(node).signed:
                return False
            wrap_unsigned(node)
            return True
        return False
    if op == 'un' and node.a == '-':
        wrap_unsigned(node)
        return True
    if op == 'asg' and node.a != '=':
        lv, rhs = node.k
        value = E('bin', [clone(lv), rhs], node.a[:-1], id=node.id)
        if value.a in DIV:
            safe_divisor(value)
        elif value.a == '*' and not ty(value).signed:
            value.k[1] = E('bin', [value.k[1], mk_const(0x7f, I16)], '&')
        elif ty(value).signed and (value.a in ARITH or value.a == '<<'):
            wrap_unsigned(value)
        node.a, node.k = '=', [lv, value]
        return True
    if op == 'incdec':
        lv = node.k[0]
        t = ty(lv)
        u = UNSIGNED_OF[t]
        step = E('bin', [E('cast', [clone(lv)], t=u), mk_const(1, I16)],
                 '+' if node.a[0] == '++' else '-')
        node.op, node.a, node.k = 'asg', '=', [lv, E('cast', [step], t=t)]
        return True
    return False


def repair(prog, trips):
    """Repair every node whose id tripped. Returns the ids left unrepaired."""
    trips = set(trips)
    left = set(trips)
    for slot in expr_slots(prog):
        node = slot_get(slot)
        if node.id in trips and node.op != 'cast' and repair_node(node):
            left.discard(node.id)
    return left


# ---------------------------------------------------------------- rendering

CHECK_PRELUDE = r'''
/* host-only checking layer: records operations that are undefined, or that
 * would differ with a 16-bit int. */
#include <stdlib.h>
static unsigned char ck_trip[%(nid)d + 1];
static void ck_hit(int id) { ck_trip[id] = 1; }
static void ck_report(void)
{
    int i, any = 0;
    for (i = 0; i <= %(nid)d; i++)
        if (ck_trip[i]) { fprintf(stderr, "%%sTRIP %%d", any ? " " : "", i); any = 1; }
    if (any) fprintf(stderr, "\n");
}
static int16_t ck_s16(int id, int32_t v) { if (v < -32768 || v > 32767) ck_hit(id); return (int16_t)v; }
static int32_t ck_s32(int id, int64_t v) { if (v < INT32_MIN || v > INT32_MAX) ck_hit(id); return (int32_t)v; }
static int ck_cnt(int id, int64_t n, int bits) { if (n < 0 || n >= bits) ck_hit(id); return (int)(n & (bits - 1)); }
static int32_t ck_div32(int id, int32_t a, int32_t b) { if (b == 0 || (a == INT32_MIN && b == -1)) { ck_hit(id); return a; } return a / b; }
static int32_t ck_mod32(int id, int32_t a, int32_t b) { if (b == 0 || (a == INT32_MIN && b == -1)) { ck_hit(id); return 0; } return a %% b; }
static uint32_t ck_udiv32(int id, uint32_t a, uint32_t b) { if (b == 0) { ck_hit(id); return a; } return a / b; }
static uint32_t ck_umod32(int id, uint32_t a, uint32_t b) { if (b == 0) { ck_hit(id); return 0; } return a %% b; }
static int16_t ck_div16(int id, int32_t a, int32_t b) { if (b == 0 || (a == -32768 && b == -1)) { ck_hit(id); return (int16_t)a; } return ck_s16(id, a / b); }
static int16_t ck_mod16(int id, int32_t a, int32_t b) { if (b == 0 || (a == -32768 && b == -1)) { ck_hit(id); return 0; } return ck_s16(id, a %% b); }
static uint16_t ck_udiv16(int id, uint16_t a, uint16_t b) { if (b == 0) { ck_hit(id); return a; } return (uint16_t)(a / b); }
static uint16_t ck_umod16(int id, uint16_t a, uint16_t b) { if (b == 0) { ck_hit(id); return 0; } return (uint16_t)(a %% b); }
static uint16_t ck_umul16(int id, uint16_t a, uint16_t b) { uint32_t p = (uint32_t)a * b; if (p > (uint32_t)INT32_MAX) ck_hit(id); return (uint16_t)p; }
static int32_t ck_shl32(int id, int32_t a, int n) { if (a < 0 || (n && (a >> (31 - n)) != 0)) ck_hit(id); return (int32_t)((uint32_t)a << n); }
static int16_t ck_shl16(int id, int32_t a, int n) { if (a < 0 || ((int64_t)a << n) > 32767) ck_hit(id); return (int16_t)((uint32_t)a << n); }
static int16_t ck_post16(int id, int16_t *p, int d) { int16_t v = *p; *p = ck_s16(id, (int32_t)v + d); return v; }
static int32_t ck_post32(int id, int32_t *p, int d) { int32_t v = *p; *p = ck_s32(id, (int64_t)v + d); return v; }
'''

HARNESS = r'''
static void hex4(uint16_t v)
{
    int8_t k;
    for (k = 12; k >= 0; k -= 4)
        putchar("0123456789abcdef"[(v >> k) & 15]);
}

static void show(uint16_t id, uint32_t v)
{
    hex4(id);
    putchar(' ');
    hex4((uint16_t)(v >> 16));
    hex4((uint16_t)v);
    putchar('\n');
}

static uint32_t ck;

static void mix(uint32_t v)
{
    ck = ((ck << 5) | (ck >> 27)) ^ v;
    ck = ck + (uint32_t)0x9e3779b1;
}
'''


FLOAT_CHECK_PRELUDE = r'''
/* float programs: a result must be finite and, unless zero, of moderate
 * magnitude, so neither compiler meets infinities, NaNs or subnormals; a
 * conversion to an integer type must be in range (C leaves it undefined). */
#include <math.h>
static float ck_f(int id, float v)
{
    if (!isfinite(v) || (v != 0.0f && (fabsf(v) < 1e-18f || fabsf(v) > 1e18f))) ck_hit(id);
    return v;
}
#define CK_F2I(name, type, lo, hi) \
    static type name(int id, float v) { if (!(v > lo && v < hi)) { ck_hit(id); return 0; } return (type)v; }
CK_F2I(ck_f2i8, int8_t, -129.0f, 128.0f)
CK_F2I(ck_f2u8, uint8_t, -1.0f, 256.0f)
CK_F2I(ck_f2i16, int16_t, -32769.0f, 32768.0f)
CK_F2I(ck_f2u16, uint16_t, -1.0f, 65536.0f)
CK_F2I(ck_f2i32, int32_t, -2147483904.0f, 2147483648.0f)
CK_F2I(ck_f2u32, uint32_t, -1.0f, 4294967296.0f)
static float ck_fpost(int id, float *p, float d) { float v = *p; *p = ck_f(id, v + d); return v; }
'''

FLOAT_HARNESS = r'''
static float fclamp(float x, float b) { return x > b ? b : (x < -b ? -b : x); }
/* a float is shown by its IEEE bits; -0 is shown as 0 */
static void showf(uint16_t id, float f)
{
    union { float f; uint32_t u; } u;
    u.f = (f == 0.0f) ? 0.0f : f;
    show(id, u.u);
}

static void mixf(float f)
{
    union { float f; uint32_t u; } u;
    u.f = (f == 0.0f) ? 0.0f : f;
    mix(u.u);
}
'''


def const_text(v, t):
    if t is F32:
        return float_text(v)
    if t is I16 or t.bits < 16:
        if t is not I16:
            return '((%s)%d)' % (t.cname, v)
        if v == -32768:
            return '(-32767 - 1)'
        return str(v) if v >= 0 else '(%d)' % v
    if t is U16:
        return '((uint16_t)%d)' % v if v < 32768 else '((uint16_t)0x%x)' % v
    if t is I32:
        if v == -(1 << 31):
            return '((int32_t)0x80000000)'
        return '((int32_t)%d)' % v
    if v >= 1 << 31:
        return '((uint32_t)0x%x)' % v
    return '((uint32_t)%d)' % v


def static_text(v, t):
    """A constant for a static initializer."""
    if t is F32:
        # bare, not parenthesised: BUG-040
        return float_text(v).strip('()')
    if t.signed:
        if v == -(1 << 31):
            return '(-2147483647 - 1)'
        return str(v)
    return '0x%x' % v if v > 32767 else str(v)


def index_mask(n):
    """Largest 2^k - 1 that is < n (n >= 1)."""
    m = 1
    while m * 2 <= n:
        m *= 2
    return m - 1


class Renderer:
    def __init__(self, checked=False, off=frozenset()):
        self.checked = checked
        self.off = off

    # ---- expressions

    def cast(self, cname, e):
        """(cname)e. With `castsfx` off the value of an assignment or ++/--
        is passed through `+ 0` first, which changes nothing but keeps the
        cast off the assignment itself."""
        s = self.ex(e)
        if e.op in ('asg', 'incdec') and 'castsfx' in self.off and is_int(ty(e)):
            s = '(%s + 0)' % s
        return '((%s)%s)' % (cname, s)

    def index(self, e, n):
        """An index into n elements."""
        if e.op == 'const' and 0 <= e.a < n:
            return str(e.a)
        if e.op == 'var' and e.a.rng is not None and e.a.rng < n and not self.checked:
            return e.a.name
        if e.op == 'var' and e.a.rng is not None and e.a.rng < n:
            return 'ck_idx(%s, %d)' % (e.a.name, n)
        return '%s & %d' % (self.ex(e), index_mask(n))

    def u16(self, e):
        return self.ex(e) if promote(ty(e)) is U16 else self.cast('uint16_t', e)

    def ex(self, e):
        return getattr(self, 'ex_' + e.op)(e)

    def ex_const(self, e):
        return const_text(e.a, e.t)

    def ex_var(self, e):
        return e.a.name

    def ex_idx(self, e):
        return e.a.name + ''.join('[%s]' % self.index(i, n) for i, n in zip(e.k, e.a.dims))

    def ex_fld(self, e, raw=False):
        base = e.k[0]
        if base.op == 'deref' and base.k[0].op == 'var':
            s = '%s->%s' % (base.k[0].a.name, e.a.name)
        else:
            s = '%s.%s' % (self.ex(base), e.a.name)
        if e.a.dims:
            s += '[%s]' % self.index(e.k[1], e.a.dims[0])
        if e.a.bf and not raw and 'bfpromo' in self.off:
            # with `bfpromo` off a bit-field is read through a cast to the
            # type it promotes to, so nothing depends on dcc promoting it
            s = '((int16_t)%s)' % s
        return s

    def lv(self, e):
        """An lvalue: a bit-field is never read through a cast here."""
        return self.ex_fld(e, raw=True) if e.op == 'fld' else self.ex(e)

    def ex_deref(self, e):
        return '(*%s)' % self.ex(e.k[0])

    def ex_pidx(self, e):
        return '%s[%s]' % (e.a.name, self.index(e.k[0], e.a.valid))

    def ex_addr(self, e):
        d, need = e.a
        return '(&%s[%s])' % (d.name, self.index(e.k[0], d.dims[0] - need + 1))

    def ex_aend(self, e):
        return '(%s + %d)' % (e.a.name, e.a.dims[0])

    def ex_addrof(self, e):
        return '(&%s)' % e.a.name

    def ex_cast(self, e):
        return self.cast(e.t.cname, e.k[0])

    def ex_un(self, e):
        a = self.ex(e.k[0])
        if e.a == '!':
            return '(!%s)' % a
        t = ty(e)
        if e.a == '-' and self.checked and t.signed:
            if t is I16:
                return 'ck_s16(%d, -%s)' % (e.id, a)
            return 'ck_s32(%d, -(int64_t)%s)' % (e.id, a)
        s = '(%s%s)' % (e.a, a)
        return '((uint16_t)%s)' % s if t is U16 else s

    def count(self, e, bits, node_id):
        if e.op == 'const' and 0 <= e.a < bits:
            s = str(e.a)
        else:
            s = '(%s & %d)' % (self.ex(e), bits - 1)
        return 'ck_cnt(%d, %s, %d)' % (node_id, s, bits) if self.checked else s

    def ex_bin(self, e):
        op = e.a
        ka, kb = e.k
        ta, tb = ty(ka), ty(kb)
        if not is_int(ta):
            return '(%s %s %s)' % (self.ex(ka), op, self.ex(kb))
        if op in LOGIC:
            return '(%s %s %s)' % (self.ex(ka), op, self.ex(kb))
        if op in SHIFT:
            t = promote(ta)
            a, n = self.ex(ka), self.count(kb, t.bits, e.id)
            if op == '<<' and t.signed and self.checked:
                return 'ck_shl%d(%d, %s, %s)' % (t.bits, e.id, a, n)
            s = '(%s %s %s)' % (a, op, n)
            return '((uint16_t)%s)' % s if op == '<<' and t is U16 else s
        ct = common(ta, tb)
        if op in CMP:
            if ct is U16:
                return '(%s %s %s)' % (self.u16(ka), op, self.u16(kb))
            return '(%s %s %s)' % (self.ex(ka), op, self.ex(kb))
        if op in DIV:
            name = 'div' if op == '/' else 'mod'
            if ct is U16:
                a, b = self.u16(ka), self.u16(kb)
                if self.checked:
                    return 'ck_u%s16(%d, %s, %s)' % (name, e.id, a, b)
                return '(%s %s %s)' % (a, op, b)
            a, b = self.ex(ka), self.ex(kb)
            if self.checked:
                return 'ck_%s%s%d(%d, %s, %s)' % ('' if ct.signed else 'u', name, ct.bits,
                                                 e.id, a, b)
            return '(%s %s %s)' % (a, op, b)
        a, b = self.ex(ka), self.ex(kb)
        if self.checked and op in ARITH:
            if ct is I16:
                return 'ck_s16(%d, %s %s %s)' % (e.id, a, op, b)
            if ct is I32:
                return 'ck_s32(%d, (int64_t)(int32_t)%s %s (int64_t)(int32_t)%s)' % (
                    e.id, a, op, b)
            if ct is U16 and op == '*' and promote(ta) is U16 and promote(tb) is U16:
                return 'ck_umul16(%d, %s, %s)' % (e.id, a, b)
        s = '(%s %s %s)' % (a, op, b)
        return '((uint16_t)%s)' % s if ct is U16 else s

    def ex_cond(self, e):
        s = '(%s ? %s : %s)' % (self.ex(e.k[0]), self.ex(e.k[1]), self.ex(e.k[2]))
        t = ty(e)
        return '((uint16_t)%s)' % s if t is U16 else s

    def ex_call(self, e):
        f = e.a
        name = '(*%s_p)' % f.name if f.via_ptr and e.id % 2 else f.name
        return '%s(%s)' % (name, ', '.join(self.ex(k) for k in e.k))

    def ex_asg(self, e):
        lv, rhs = e.k
        op = e.a
        left, right = self.lv(lv), self.ex(rhs)
        if op == '=':
            return '(%s = %s)' % (left, right)
        lt = ty(lv)
        if not is_int(lt):
            return '(%s %s %s)' % (left, op, right)
        bop = op[:-1]
        back = '' if is_bitfield(lv) else '(%s)' % lt.cname
        if bop in SHIFT:
            t = promote(lt)
            n = self.count(rhs, t.bits, e.id)
            if bop == '<<' and t.signed and self.checked:
                return '(%s = %sck_shl%d(%d, %s, %s))' % (left, back, t.bits, e.id, left, n)
            return '(%s %s %s)' % (left, op, n)
        ct = common(lt, ty(rhs))
        if bop in DIV:
            name = 'div' if bop == '/' else 'mod'
            if ct is U16:
                b = self.u16(rhs)
                if promote(lt) is not U16:
                    # the target is converted to unsigned for the operation;
                    # a compound assignment cannot spell that on the host
                    a = '((uint16_t)%s)' % left
                    if self.checked:
                        return '(%s = %sck_u%s16(%d, %s, %s))' % (left, back, name, e.id, a, b)
                    return '(%s = %s(%s %s %s))' % (left, back, a, bop, b)
                if self.checked:
                    return '(%s = %sck_u%s16(%d, %s, %s))' % (left, back, name, e.id, left, b)
                return '(%s %s %s)' % (left, op, b)
            if self.checked:
                return '(%s = %sck_%s%s%d(%d, %s, %s))' % (
                    left, back, '' if ct.signed else 'u', name, ct.bits, e.id, left, right)
            return '(%s %s %s)' % (left, op, right)
        if self.checked and bop in ARITH:
            if ct is I16:
                return '(%s = %sck_s16(%d, %s %s %s))' % (left, back, e.id, left, bop, right)
            if ct is I32:
                return '(%s = %sck_s32(%d, (int64_t)(int32_t)%s %s (int64_t)(int32_t)%s))' % (
                    left, back, e.id, left, bop, right)
            if ct is U16 and bop == '*' and promote(lt) is U16 and promote(ty(rhs)) is U16:
                return '(%s = %sck_umul16(%d, %s, %s))' % (left, back, e.id, left, right)
        return '(%s %s %s)' % (left, op, right)

    def ex_incdec(self, e):
        lv = e.k[0]
        op, prefix = e.a
        left = self.lv(lv)
        lt = ty(lv)
        if self.checked and is_int(lt) and lt in (I16, I32) and not is_bitfield(lv):
            d = 1 if op == '++' else -1
            if prefix:
                if lt is I16:
                    return '(%s = ck_s16(%d, %s + %d))' % (left, e.id, left, d)
                return '(%s = ck_s32(%d, (int64_t)%s + %d))' % (left, e.id, left, d)
            return 'ck_post%d(%d, &%s, %d)' % (lt.bits, e.id, left, d)
        return '(%s%s)' % (op, left) if prefix else '(%s%s)' % (left, op)

    # ---- statements

    def top(self, e):
        """An expression statement or controlling expression, unparenthesized."""
        s = self.ex(e)
        if s.startswith('(') and s.endswith(')'):
            depth = 0
            for i, ch in enumerate(s):
                depth += ch == '('
                depth -= ch == ')'
                if depth == 0 and i < len(s) - 1:
                    return s
            return s[1:-1]
        return s

    def decl(self, d):
        if isinstance(d.t, PtrType):
            s = '%s *%s' % (d.t.to.cname, d.name)
        else:
            s = '%s %s%s' % (d.cname, d.name, ''.join('[%d]' % n for n in d.dims))
        if d.static:
            s = 'static ' + s
        if isinstance(d.init, E):
            s += ' = ' + self.top(d.init)
        elif d.init is not None:
            s += ' = ' + self.init(d.init, d.t, d.dims)
        return s

    def init(self, v, t, dims):
        if dims:
            return '{ %s }' % ', '.join(self.init(x, t, dims[1:]) for x in v)
        if isinstance(t, StructType):
            return '{ %s }' % ', '.join(self.init(x, f.t, f.dims) for x, f in zip(v, t.fields))
        return static_text(v, t)

    def block(self, b, out, ind):
        for d in b.decls:
            out.append('%s%s;' % (ind, self.decl(d)))
        for s in b.stmts:
            self.stmt(s, out, ind)

    def braced(self, b, out, ind, head, tail=''):
        out.append('%s%s {' % (ind, head))
        self.block(b, out, ind + '    ')
        out.append('%s}%s' % (ind, tail))

    def stmt(self, s, out, ind):
        op = s.op
        if op == 'expr':
            out.append('%s%s;' % (ind, self.top(s.e[0])))
        elif op == 'if':
            self.braced(s.b[0], out, ind, 'if (%s)' % self.top(s.e[0]))
            if len(s.b) > 1 and s.b[1] is not None:
                out[-1] += ' else {'
                self.block(s.b[1], out, ind + '    ')
                out.append(ind + '}')
        elif op == 'for':
            if s.a is not None:
                init = self.decl(s.a)
            else:
                init = self.top(s.e[0]) if s.e[0] is not None else ''
            test = self.top(s.e[1]) if s.e[1] is not None else ''
            step = self.top(s.e[2]) if s.e[2] is not None else ''
            self.braced(s.b[0], out, ind, 'for (%s; %s; %s)' % (init, test, step))
        elif op == 'while':
            self.braced(s.b[0], out, ind, 'while (%s)' % self.top(s.e[0]))
        elif op == 'do':
            self.braced(s.b[0], out, ind, 'do', ' while (%s);' % self.top(s.e[0]))
        elif op == 'switch':
            out.append('%sswitch (%s) {' % (ind, self.top(s.e[0])))
            for (labels, has_break), body in zip(s.a, s.b):
                if labels is None:
                    out.append(ind + 'default:')
                else:
                    for v in labels:
                        out.append('%scase %d:' % (ind, v))
                self.block(body, out, ind + '    ')
                if has_break:
                    out.append(ind + '    break;')
            if out[-1].endswith(':'):
                # a label needs a statement after it before C23
                out.append(ind + '    ;')
            out.append(ind + '}')
        elif op in ('break', 'continue'):
            out.append('%s%s;' % (ind, op))
        elif op == 'ret':
            out.append('%sreturn%s;' % (ind, ' ' + self.top(s.e[0]) if s.e else ''))
        elif op == 'block':
            out.append(ind + '{')
            self.block(s.b[0], out, ind + '    ')
            out.append(ind + '}')
        elif op == 'goto':
            out.append('%sif (%s) goto %s;' % (ind, self.top(s.e[0]), s.a))
        elif op == 'label':
            out.append('%s:;' % s.a)
        else:
            raise ValueError(op)

    # ---- program

    def signature(self, f):
        ret = 'void' if f.ret is None else f.ret.cname
        params = ', '.join(self.decl(p) for p in f.params) or 'void'
        return '%s%s %s(%s)' % ('static ' if f.static else '', ret, f.name, params)

    def state(self, prog, out):
        out.append('static uint32_t state(void)')
        out.append('{')
        out.append('    uint8_t i, j;')
        out.append('    ck = 0;')

        def leaf(text, t, dims, ind, var):
            if dims:
                out.append('%sfor (%s = 0; %s < %d; %s++)' % (ind, var, var, dims[0], var))
                leaf('%s[%s]' % (text, var), t, dims[1:], ind + '    ', 'j')
            elif isinstance(t, StructType):
                out.append(ind + '{')
                for f in t.fields:
                    leaf('%s.%s' % (text, f.name), f.t, f.dims, ind + '    ', 'j')
                out.append(ind + '}')
            elif t is F32:
                out.append('%smixf(%s);' % (ind, text))
            else:
                out.append('%smix((uint32_t)%s);' % (ind, text))

        for g in prog.globals:
            if isinstance(g.t, PtrType):
                continue
            leaf(g.name, g.t, g.dims, '    ', 'i')
        out.append('    return ck;')
        out.append('}')

    def program(self, prog, title=''):
        out = ['/* generated by clang-oracle-fuzz.py%s */' % title,
               '#include <stdint.h>', '#include <stdio.h>']
        if self.checked:
            out.append(CHECK_PRELUDE % {'nid': prog.nid})
            out.append('#define ck_idx(v, n) ((v) < 0 || (v) >= (n) ? (abort(), 0) : (v))')
            if prog.floats:
                out.append(FLOAT_CHECK_PRELUDE)
        out.append('')
        for st in prog.structs:
            out.append('struct %s {' % st.name)
            for f in st.fields:
                dims = ''.join('[%d]' % n for n in f.dims)
                bits = ' : %d' % f.bf if f.bf else ''
                out.append('    %s %s%s%s;' % (f.t.cname, f.name, dims, bits))
            out.append('};')
        out.append('')
        for g in prog.globals:
            out.append('%s;' % self.decl(g))
        out.append(HARNESS)
        if prog.floats:
            out.append(FLOAT_HARNESS)
        self.state(prog, out)
        out.append('')
        for f in prog.funcs:
            if f.kind == 'rec' or f.via_ptr:
                out.append(self.signature(f) + ';')
        for f in prog.funcs:
            if f.via_ptr:
                ret = 'void' if f.ret is None else f.ret.cname
                params = ', '.join(p.cname for p in f.params) or 'void'
                out.append('static %s (*%s_p)(%s) = %s;' % (ret, f.name, params, f.name))
        for f in prog.funcs:
            out.append('')
            out.append(self.signature(f))
            out.append('{')
            self.block(f.body, out, '    ')
            out.append('}')
        out.append('')
        out.append('int main(void)')
        out.append('{')
        n = 0
        for f in prog.tests:
            for args in f.inputs:
                n += 1
                call = '%s(%s)' % (f.name, ', '.join(self.ex(a) for a in args))
                if is_int(f.ret):
                    out.append('    show(%d, (uint32_t)%s);' % (n, call))
                elif f.ret is F32:
                    out.append('    showf(%d, %s);' % (n, call))
                else:
                    out.append('    %s;' % call)
                out.append('    show(%d, state());' % (n + 0x4000))
        if self.checked:
            out.append('    ck_report();')
        out.append('    return 0;')
        out.append('}')
        return '\n'.join(out) + '\n'


# --------------------------------------------------------------- generation

POOL = [0, 1, 2, 3, 5, 7, 8, 15, 16, 31, 63, 100, 127, 128, 129, 255, 256, 257, 1000,
        0x7ffe, 0x7fff, 0x8000, 0x8001, 0xfffe, 0xffff, 0x10000, 0x10001, 0x12345,
        0x7fffffff, 0x80000000, 0x80000001, 0xfffffffe, 0xffffffff, 0x00ff00ff,
        0xdeadbeef, 0x55aa, 0xaa55, -1, -2, -3, -100, -128, -129, -32768, -32769]


class Ctx:
    def __init__(self, fn, pure):
        self.fn = fn
        self.pure = pure
        self.ints, self.arrs, self.sarrs, self.svars = [], [], [], []
        self.ptrs, self.sptrs, self.walk = [], [], []
        self.counters = []
        self.prot = set()
        self.used, self.banned = set(), set()
        self.loop = 0
        self.in_if = 0
        self.arm = 0
        self.nosfx = 0
        self.names = 0
        self.labels = 0

    def mark(self):
        return [len(x) for x in (self.ints, self.arrs, self.sarrs, self.svars, self.ptrs,
                                 self.sptrs, self.walk, self.counters)]

    def release(self, mark):
        for lst, n in zip((self.ints, self.arrs, self.sarrs, self.svars, self.ptrs,
                           self.sptrs, self.walk, self.counters), mark):
            del lst[n:]

    def can_write(self, d):
        """Whether d (or, for a pointer, what it points at) may be stored to."""
        if isinstance(d.t, PtrType):
            return d.tgt_local or not self.pure
        if d in self.prot or d in self.banned:
            return False
        return d.storage != 'global' or not self.pure

    def name(self, prefix):
        self.names += 1
        return '%s%d' % (prefix, self.names)

    def fresh(self):
        self.used, self.banned = set(), set()


FEATURES = frozenset((
    'bitfield', 'nested', 'arrfield', 'struct', 'structval', 'ptr', 'arr2d', 'localarr',
    'if', 'for', 'while', 'do', 'switch', 'block', 'call', 'scopy', 'walk', 'brk', 'ret',
    'goto', 'c99for', 'blockdecl', 'static', 'varbound',
    'logic', 'un', 'cast', 'cond', 'shift', 'div', 'pcall', 'sfx', 'fnptr', 'rec',
    'cassign', 'incdec', 'i8', 'i32',
    # forms that hit a logged dcc defect; see KNOWN_BUGS
    'widecassign', 'bytevarshl', 'castsfx', 'widepostinc', 'structbytecasg', 'negnotconst',
    'bfdivasg', 'wideidxincdec', 'logicinit', 'blockwidediv', 'ptrcallconv',
    'blockwideneg', 'nestedwideless', 'bfpromo', 'widelocalinc', 'blockptr', 'inittype',
    'selfand', 'widecasgbf', 'widehelperparam', 'postincvalue', 'duplabel', 'crosstypeasg',
    'constjump', 'blockret', 'selfoperand', 'nestedifelse', 'bytesarcount', 'bytearm',
    'armfordecl', 'blockwidecopy', 'constdivzero', 'peepsum', 'blockcast',
    'blockdec', 'loopcallarg', 'loopcopy', 'wordarm', 'peepbyteself'))

# Features switched off by `--off known`, with the entry in compiler-bugs.md
# that each one would otherwise keep rediscovering. Remove a feature from this
# table when its bug is fixed.
KNOWN_BUGS = {
    'bytevarshl': 'BUG-002',
    'logicinit': 'BUG-004',
    'bfdivasg': 'BUG-006',
    'nestedwideless': 'BUG-009',
    'widepostinc': 'BUG-010',
    'bfpromo': 'BUG-013',
    'blockptr': 'BUG-015',
    'inittype': 'BUG-016',
    'widehelperparam': 'BUG-020',
    'duplabel': 'BUG-022',
    'constjump': 'BUG-019',
    'nestedifelse': 'BUG-027',
    'bytesarcount': 'BUG-028',
    'bytearm': 'BUG-029',
    'constdivzero': 'BUG-033',
    'blockcast': 'BUG-015',
    'blockdec': 'BUG-015',
    'loopcopy': 'BUG-037',
    'wordarm': 'BUG-038',
}


class Gen:
    def __init__(self, seed, tests=6, size=10, depth=3, off=frozenset()):
        self.rng = random.Random(seed)
        self.prog = Program(seed, off)
        self.n_tests, self.size, self.depth = tests, size, depth
        self.off = frozenset(off)
        self.pure_fns, self.impure_fns, self.struct_fns = [], [], []

    def on(self, feature):
        return feature not in self.off

    # ---- small helpers

    def mk(self, op, k=(), a=None, t=None):
        if op in ('bin', 'asg') and not self.on('peepbyteself') and peep_byte_self(a, k):
            self.prog.nid += 1
            k = [k[0], E('const', (), 1, I16, self.prog.nid)]
        if op == 'bin' and not self.on('selfoperand') and same_variable(k[0], k[1]):
            # one variable is never both operands of an operator
            self.prog.nid += 1
            k = [k[0], E('const', (), 1, I16, self.prog.nid)]
        if op == 'bin' and a in DIV and not self.on('constdivzero') \
                and k[1].op == 'const' and k[1].a == 0 and is_constant(k[0]):
            # the checking layer only repairs a division that runs
            self.prog.nid += 1
            k = [k[0], E('const', (), 1, I16, self.prog.nid)]
        if op == 'incdec' and not self.on('blockdec') and k[0].op == 'var' \
                and k[0].a.storage == 'block' and ty(k[0]) is U16:
            # a nested-block unsigned 16-bit local is changed with a compound assignment
            return self.mk('asg', [k[0], self.small(1, 1)], a[0][0] + '=')
        if op == 'call' and not self.on('loopcallarg'):
            k = [self.mk('bin', [x, self.small(1, 1)], '^') if x.op == 'var'
                 and x.a.storage != 'global' and is_int(x.a.t) and x.a.t.bits == 32 else x
                 for x in k]
        if op == 'bin' and not self.on('blockcast'):
            for i in (0, 1):
                c = k[i]
                if c.op == 'cast' and c.t is I16 and ty(c.k[0]) is U16 and has_block_wide(k[1 - i]):
                    # compare and combine the unsigned value without the signed cast
                    k = list(k)
                    k[i] = self.mk('cast', [c.k[0]], t=I32)
        self.prog.nid += 1
        return E(op, k, a, t, self.prog.nid)

    def pick(self, weighted):
        total = sum(w for _, w in weighted)
        x = self.rng.random() * total
        for item, w in weighted:
            x -= w
            if x < 0:
                return item
        return weighted[-1][0]

    def int_type(self):
        return self.pick([(I8, 3 if self.on('i8') else 0), (U8, 4 if self.on('i8') else 0),
                          (I16, 5), (U16, 5), (I32, 5 if self.on('i32') else 0),
                          (U32, 4 if self.on('i32') else 0)])

    def value(self, t):
        r = self.rng
        if r.random() < 0.7:
            return t.wrap(r.choice(POOL))
        return t.wrap(r.getrandbits(32) if r.random() < 0.5 else r.randint(-300, 300))

    def const(self, t=None):
        if t is None:
            t = self.pick([(I16, 8), (U16, 3), (I32, 5 if self.on('i32') else 0),
                           (U32, 4 if self.on('i32') else 0)])
        if t.bits < 16:
            t = I16 if t.signed else U16
            return self.mk('const', a=self.rng.randint(0, 127), t=t)
        return self.mk('const', a=self.value(t), t=t)

    def small(self, lo, hi):
        return self.mk('const', a=self.rng.randint(lo, hi), t=I16)

    def spelled(self, t):
        return self.rng.choice(t.cnames)

    # ---- program skeleton

    def gen_structs(self):
        r = self.rng
        for n in range(2):
            fields = []
            for i in range(r.randint(3, 6)):
                name = 'm%d' % i
                kind = r.random()
                if kind < 0.2 and self.on('bitfield'):
                    t = r.choice((I16, U16))
                    width = r.randint(2 if t.signed else 1, 15)
                    fields.append(Field(name, t, bf=width))
                elif kind < 0.35 and self.on('arrfield'):
                    fields.append(Field(name, self.int_type(), dims=(r.choice((2, 4)),)))
                elif kind < 0.45 and n == 1 and self.on('nested'):
                    fields.append(Field(name, self.prog.structs[0]))
                else:
                    fields.append(Field(name, self.int_type()))
            self.prog.structs.append(StructType('S%d' % n, fields))

    def init_value(self, t, dims):
        if dims:
            return [self.init_value(t, dims[1:]) for _ in range(dims[0])]
        if isinstance(t, StructType):
            return [self.field_value(f) for f in t.fields]
        return self.value(t)

    def field_value(self, f):
        if f.bf:
            lo = -(1 << (f.bf - 1)) if f.t.signed else 0
            hi = (1 << (f.bf - 1)) - 1 if f.t.signed else (1 << f.bf) - 1
            return self.rng.randint(lo, hi)
        return self.init_value(f.t, f.dims)

    def gen_globals(self):
        r = self.rng
        g = self.prog.globals
        n = 0
        allowed = lambda t: self.on('i8' if t.bits == 8 else 'i32' if t.bits == 32 else '')
        for t in INT_TYPES:
            if not allowed(t):
                continue
            for _ in range(r.randint(1, 2)):
                d = Decl('g%d' % n, t, 'global', init=self.value(t), cname=self.spelled(t))
                d.static = r.random() < 0.6
                g.append(d)
                n += 1
        for i, (t, dims) in enumerate([(U8, (8,)), (I16, (8,)), (U16, (4,)), (I32, (4,)),
                                       (U32, (8,)), (r.choice((U8, I16)), (4, 4)),
                                       (I8, (8,))]):
            if not allowed(t) or (len(dims) > 1 and not self.on('arr2d')):
                continue
            d = Decl('a%d' % i, t, 'global', dims=dims, init=self.init_value(t, dims))
            d.static = r.random() < 0.6
            g.append(d)
        if not self.on('struct'):
            return
        s0, s1 = self.prog.structs
        for i, (t, dims) in enumerate([(s0, ()), (s1, ()), (r.choice((s0, s1)), (4,))]):
            d = Decl('s%d' % i, t, 'global', dims=dims, init=self.init_value(t, dims))
            d.static = True
            g.append(d)

    def ctx(self, fn, pure):
        c = Ctx(fn, pure)
        for d in self.prog.globals:
            self.expose(c, d)
        return c

    def expose(self, c, d):
        if isinstance(d.t, PtrType):
            (c.sptrs if isinstance(d.t.to, StructType) else c.ptrs).append(d)
        elif isinstance(d.t, StructType):
            (c.sarrs if d.dims else c.svars).append(d)
        elif d.dims:
            c.arrs.append(d)
        else:
            c.ints.append(d)

    # ---- expressions

    def gen_index(self, c, depth, n):
        r = self.rng
        counters = [d for d in c.counters if d.rng is not None and d.rng < n]
        x = r.random()
        if counters and x < 0.45:
            d = r.choice(counters)
            c.used.add(d)
            return self.mk('var', a=d)
        if x < 0.6:
            return self.small(0, n - 1)
        return self.gen_expr(c, min(depth - 1, 1))

    def struct_lvalue(self, c, depth, write):
        r = self.rng
        opts = [('var', d) for d in c.svars if not write or c.can_write(d)]
        opts += [('arr', d) for d in c.sarrs if not write or c.can_write(d)]
        opts += [('ptr', d) for d in c.sptrs if not write or c.can_write(d)]
        if not opts:
            return None
        kind, d = r.choice(opts)
        if kind == 'var':
            return self.mk('var', a=d)
        if kind == 'arr':
            return self.mk('idx', [self.gen_index(c, depth, d.dims[0])], a=d)
        return self.mk('deref', [self.mk('var', a=d)])

    def field_of(self, c, depth, base, st, nest=True):
        r = self.rng
        fields = [f for f in st.fields if nest or not isinstance(f.t, StructType)]
        f = r.choice(fields)
        node = self.mk('fld', [base], a=f)
        if isinstance(f.t, StructType):
            return self.field_of(c, depth, node, f.t, nest=False)
        if f.dims:
            node.k.append(self.gen_index(c, depth, f.dims[0]))
        return node

    def gen_lvalue(self, c, depth, write=False):
        """An integer lvalue. One that will be stored to has no side effects
        in its index expressions: a repair may duplicate the lvalue
        (`x op= e` becomes `x = x op e`), and a duplicated `i++` would be an
        unsequenced modification."""
        if not write:
            return self.pick_lvalue(c, depth, False)
        c.nosfx += 1
        try:
            return self.pick_lvalue(c, depth, True)
        finally:
            c.nosfx -= 1

    def pick_lvalue(self, c, depth, write):
        r = self.rng
        ints = [d for d in c.ints if d not in c.banned and (not write or c.can_write(d))]
        arrs = [d for d in c.arrs if not write or c.can_write(d)]
        ptrs = [d for d in c.ptrs + c.walk if not write or c.can_write(d)]
        has_struct = any(not write or c.can_write(d) for d in c.svars + c.sarrs + c.sptrs)
        kind = self.pick([('var', 6 if ints else 0), ('idx', 3 if arrs else 0),
                          ('fld', 3 if has_struct else 0), ('ptr', 2 if ptrs else 0)])
        if kind == 'var' and ints:
            if not write:
                locals_ = [d for d in ints if d.storage != 'global']
                if locals_ and r.random() < 0.6:
                    ints = locals_
            d = r.choice(ints)
            c.used.add(d)
            return self.mk('var', a=d)
        if kind == 'idx' and arrs:
            d = r.choice(arrs)
            return self.mk('idx', [self.gen_index(c, depth, n) for n in d.dims], a=d)
        if kind == 'fld':
            base = self.struct_lvalue(c, depth, write)
            if base is not None:
                return self.field_of(c, depth, base, ty(base))
        if kind == 'ptr' and ptrs:
            d = r.choice(ptrs)
            if d in c.walk or r.random() < 0.4:
                return self.mk('deref', [self.mk('var', a=d)])
            return self.mk('pidx', [self.gen_index(c, depth, d.valid)], a=d)
        if ints:
            d = r.choice(ints)
            c.used.add(d)
            return self.mk('var', a=d)
        return None

    def gen_leaf(self, c, depth):
        if self.rng.random() < 0.22:
            return self.const()
        return self.gen_lvalue(c, depth) or self.const()

    def victim(self, c):
        cands = [d for d in c.ints if d.storage != 'global' and not d.addr_taken
                 and d not in c.prot and d not in c.used and d not in c.banned]
        if not cands:
            return None
        d = self.rng.choice(cands)
        c.used.add(d)
        c.banned.add(d)
        return d

    def gen_expr(self, c, depth):
        r = self.rng
        if depth <= 0 or r.random() < 0.14:
            return self.gen_leaf(c, depth)
        w = lambda feature, weight: weight if self.on(feature) else 0
        kind = self.pick([('arith', 30), ('bits', 14), ('cmp', 10), ('logic', w('logic', 5)),
                          ('un', w('un', 7)), ('cast', w('cast', 12)), ('cond', w('cond', 7)),
                          ('shift', w('shift', 9)), ('div', w('div', 6)),
                          ('call', w('pcall', 9) if self.pure_fns else 0),
                          ('sfx', w('sfx', 5) if not c.nosfx else 0)])
        sub = lambda: self.gen_expr(c, depth - 1)
        if kind == 'arith':
            return self.mk('bin', [sub(), sub()], r.choice(('+', '+', '-', '-', '*')))
        if kind == 'bits':
            return self.mk('bin', [sub(), sub()], r.choice(BITS))
        if kind == 'cmp':
            return self.mk('bin', [sub(), sub()], r.choice(CMP))
        if kind == 'logic':
            return self.mk('bin', [sub(), sub()], r.choice(LOGIC))
        if kind == 'shift':
            count = self.small(0, 17) if r.random() < 0.6 else sub()
            return self.mk('bin', [sub(), count], r.choice(SHIFT))
        if kind == 'div':
            x = r.random()
            if x < 0.4:
                divisor = self.small(1, 200) if r.random() < 0.8 else self.const()
            elif x < 0.7:
                divisor = self.mk('bin', [sub(), self.small(1, 1)], '|')
            else:
                divisor = sub()
            return self.mk('bin', [sub(), divisor], r.choice(DIV))
        if kind == 'un':
            op = r.choice(('-', '~', '!'))
            operand = sub()
            if op in ('-', '~') and not self.on('negnotconst') and (logic_value(operand) or (
                    operand.op != 'const' and is_constant(operand))):
                return operand
            return self.mk('un', [operand], op)
        if kind == 'cast':
            return self.mk('cast', [sub()], t=self.int_type())
        if kind == 'cond':
            return self.mk('cond', [self.gen_cond(c, depth - 1), sub(), sub()])
        if kind == 'call':
            f = r.choice(self.pure_fns)
            args = [sub() for _ in f.params]
            if f.via_ptr and not self.on('ptrcallconv'):
                args = [self.mk('cast', [a], t=p.t) for a, p in zip(args, f.params)]
            return self.mk('call', args, a=f)
        d = self.victim(c)
        if d is None:
            return self.gen_leaf(c, depth)
        lv = self.mk('var', a=d)
        x = r.random()
        if x < 0.45:
            # with `widepostinc` off a 32-bit victim is only pre-incremented:
            # its old value could reach a narrower type. Narrow postfix
            # values are restored; explicit `postincvalue` still steers bytes.
            prefix = r.random() < 0.5 or (d.t.bits == 8 and not self.on('postincvalue')) \
                or (d.t.bits == 32 and not self.on('widepostinc'))
            op = r.choice(('++', '--'))
            if d.t.bits == 32 and not self.on('widelocalinc'):
                op = '--'
            return self.mk('incdec', [lv], a=(op, prefix))
        if x < 0.75:
            return self.mk('asg', [lv, self.stored(sub(), d.t, d.storage)], '=')
        return self.compound(lv, sub(), r.choice(('+=', '-=', '^=', '|=', '&=', '>>=')))

    def gen_cond(self, c, depth):
        x = self.rng.random()
        if x < 0.65 or depth < -1:
            return self.mk('bin', [self.gen_expr(c, depth), self.gen_expr(c, depth)],
                           self.rng.choice(CMP))
        if x < 0.8:
            return self.mk('bin', [self.gen_cond(c, depth - 1), self.gen_cond(c, depth - 1)],
                           self.rng.choice(LOGIC))
        if x < 0.88:
            return self.mk('un', [self.gen_expr(c, depth)], '!')
        return self.gen_expr(c, depth)

    # ---- statements

    def new_local(self, c, prefix, t, init, top=True):
        d = Decl(c.name(prefix), t, 'local', init=init, cname=self.spelled(t))
        if top:
            c.fn.body.decls.append(d)
        return d

    def gen_block(self, c, depth, n, decls=0):
        b = Block()
        mark = c.mark()
        c.arm += 1
        if not self.on('blockdecl'):
            decls = 0
        for _ in range(decls):
            self.block_decl(c, b, depth)
        for _ in range(n):
            self.gen_stmt(c, depth, b)
        c.arm -= 1
        c.release(mark)
        return b

    def block_decl(self, c, b, depth):
        t = self.int_type()
        c.fresh()
        init, later = self.initialiser(c, t, self.gen_expr(c, min(depth, 2)), 'block')
        d = Decl(c.name('b'), t, 'block', init=init, cname=self.spelled(t))
        b.decls.append(d)
        self.assign_after(b, d, later)
        c.ints.append(d)

    def gen_stmt(self, c, depth, b):
        r = self.rng
        c.fresh()
        nest = depth > 0
        w = lambda feature, weight: weight if self.on(feature) else 0
        kind = self.pick([
            ('assign', 30), ('cassign', w('cassign', 12)), ('incdec', w('incdec', 5)),
            ('if', w('if', 13) if nest else 0), ('for', w('for', 8) if nest else 0),
            ('while', w('while', 3) if nest else 0), ('do', w('do', 3) if nest else 0),
            ('switch', w('switch', 5) if nest else 0), ('block', w('block', 5) if nest else 0),
            ('call', w('call', 7) if self.impure_fns and not c.pure else 0),
            ('scopy', w('scopy', 4)), ('walk', w('walk', 3) if nest and self.on('ptr') else 0),
            ('brk', w('brk', 4) if c.loop else 0), ('ret', w('ret', 2)),
            ('goto', w('goto', 2) if nest else 0),
            ('ptr', 2 if c.ptrs or c.sptrs else 0)])
        getattr(self, 'st_' + kind)(c, depth, b)

    def stored(self, rhs, t, storage, init=False):
        """rhs as the value stored to a local of type t (init: as its
        initialiser), steered round logged bugs whose feature is off."""
        if storage == 'global' or not is_int(t):
            return rhs
        if not self.on('logicinit') and (t.bits <= 16 or storage == 'block') \
                and rhs.op == 'bin' and rhs.a in LOGIC:
            rhs.a = '|' if rhs.a == '||' else '&'
            if not self.on('peepbyteself') and peep_byte_self(rhs.a, rhs.k):
                rhs.k[1] = self.mk('const', a=1, t=I16)
        if not self.on('blockwideneg') and storage == 'block' and t.bits == 32 \
                and negative_constant(rhs, t):
            rhs = self.small(1, 1)
        if not self.on('blockwidecopy') and storage == 'block' and t.bits == 32 \
                and rhs.op == 'var' and rhs.a.storage != 'global':
            rhs = self.mk('bin', [rhs, self.small(1, 1)], '^')
        if not self.on('loopcopy') and t.bits == 32 and rhs.op == 'var' \
                and rhs.a.storage != 'global' and is_int(rhs.a.t) and rhs.a.t.bits == 32:
            rhs = self.mk('bin', [rhs, self.small(1, 1)], '^')
        if not init and not self.on('crosstypeasg') and t.bits == 16 and cross_type(rhs, t):
            # a cast that changes width as well as type keeps the value
            rhs = self.mk('cast', [self.mk('cast', [rhs], t=I32)], t=t)
        return rhs

    def initialiser(self, c, t, expr, storage):
        """(initialiser, statement or None) for a new local of type t that
        starts as expr. With `inittype` off, an expr that is not already of
        exactly type t is assigned by a following statement instead."""
        expr = self.stored(expr, t, storage)
        if not self.on('blockwidecopy') and storage == 'block' and t.bits == 32 \
                and expr.op == 'var' and expr.a.storage != 'global':
            # an expression, not a copy of the variable
            expr = self.mk('bin', [expr, self.small(1, 1)], '^')
        if self.on('inittype') or init_keeps_type(expr, t):
            return expr, None
        return mk_const(0, t if t.bits >= 16 else I16), expr

    def assign_after(self, block, d, value):
        if value is not None:
            block.stmts.append(S('expr', [self.mk('asg', [self.mk('var', a=d), value], '=')]))

    def st_assign(self, c, depth, b):
        lv = self.gen_lvalue(c, depth, write=True)
        if lv is None:
            return
        rhs = self.gen_expr(c, depth)
        if lv.op == 'var':
            rhs = self.stored(rhs, lv.a.t, lv.a.storage)
        elif not self.on('blockwidecopy') and wide_block_store(lv, rhs):
            rhs = self.mk('cast', [rhs], t=ty(lv))
        b.stmts.append(S('expr', [self.mk('asg', [lv, rhs], '=')]))

    def compound(self, lv, rhs, op):
        """lv op= rhs. A feature that is off names a form dcc is known to
        reject or miscompile (see KNOWN_BUGS); the operand is converted so the
        statement stays out of it and fuzzing can reach other code."""
        lt = ty(lv)
        if op == '=' and not self.on('blockwidecopy') and wide_block_store(lv, rhs):
            rhs = self.mk('cast', [rhs], t=lt)
        if op != '=' and is_int(lt):
            if not self.on('selfand') and lv.op == 'var' and rhs.op == 'var' and rhs.a is lv.a:
                rhs = self.small(1, 7)
            if not self.on('widelocalinc') and lv.op == 'var' and lt.bits == 32 \
                    and lv.a.storage != 'global' and op == '+=' and rhs.op == 'const' \
                    and rhs.a == 1:
                rhs = self.small(2, 2)
            if not self.on('peepsum') and lv.op == 'var' and lt.bits == 16 \
                    and lv.a.storage != 'global' and op == '+=' and rhs.op == 'const' \
                    and 2 <= rhs.a <= 255:
                rhs = self.small(1, 1)
            if not self.on('structbytecasg') and lv.op == 'fld' and lv.a.dims \
                    and lt.bits == 8:
                op = '='
                return self.mk('asg', [lv, rhs], op)
            if not self.on('bfdivasg') and is_bitfield(lv) and op in ('/=', '%=', '>>=') \
                    and not lv.a.t.signed:
                rhs = self.mk('bin', [clone(lv), rhs], op[:-1])
                return self.mk('asg', [lv, rhs], '=')
            if not self.on('blockwidediv') and lv.op == 'fld' and lt.bits == 32 \
                    and op in ('/=', '%='):
                rhs = self.mk('bin', [clone(lv), rhs], op[:-1])
                return self.mk('asg', [lv, rhs], '=')
            if not self.on('widecassign') and lv.op != 'var' and lt.bits < 32 \
                    and ty(rhs).bits == 32:
                rhs = self.mk('cast', [rhs], t=lt)
            if not self.on('widecasgbf') and ty(rhs).bits == 32 \
                    and (is_bitfield(lv) or (lv.op == 'fld' and lv.a.dims and lt.bits == 16)):
                rhs = self.mk('cast', [rhs], t=lt)
            if not self.on('bytevarshl') and op == '<<=' and lt.bits == 8 \
                    and ty(rhs).bits == 8:
                rhs = self.mk('cast', [rhs], t=I16)
            if not self.on('bytesarcount') and op == '>>=' and lt is I8 and ty(rhs) is U16:
                rhs = self.mk('cast', [rhs], t=I16)
        return self.mk('asg', [lv, rhs], op)

    def st_cassign(self, c, depth, b):
        lv = self.gen_lvalue(c, depth, write=True)
        if lv is None:
            return
        op = self.rng.choice(('+=', '+=', '-=', '-=', '*=', '/=', '%=', '&=', '|=', '^=',
                              '<<=', '>>='))
        if op in ('<<=', '>>=') and self.rng.random() < 0.6:
            rhs = self.small(0, 17)
        elif op in ('/=', '%=') and self.rng.random() < 0.6:
            rhs = self.small(1, 50)
        else:
            rhs = self.gen_expr(c, depth - 1)
        b.stmts.append(S('expr', [self.compound(lv, rhs, op)]))

    def st_incdec(self, c, depth, b):
        lv = self.gen_lvalue(c, depth, write=True)
        if lv is None:
            return
        op = self.rng.choice(('++', '--'))
        if not self.on('widelocalinc') and lv.op == 'var' and ty(lv).bits == 32 \
                and lv.a.storage != 'global':
            op = '--'
        indexed = lv.op in ('idx', 'pidx') or (lv.op == 'fld' and lv.a.dims)
        if not self.on('wideidxincdec') and indexed and ty(lv).bits == 32:
            b.stmts.append(S('expr', [self.mk('asg', [lv, self.small(1, 1)], op[0] + '=')]))
            return
        b.stmts.append(S('expr', [self.mk('incdec', [lv], a=(op, self.rng.random() < 0.5))]))

    def st_if(self, c, depth, b):
        r = self.rng
        cond = self.gen_cond(c, min(depth, 2))
        # with `nestedifelse` off an `if` inside an `if` gets no `else`
        bare = c.in_if and not self.on('nestedifelse')
        c.in_if += 1
        then = self.gen_block(c, depth - 1, r.randint(1, 3), decls=r.random() < 0.3)
        blocks = [then]
        if r.random() < 0.45 and not bare:
            blocks.append(self.gen_block(c, depth - 1, r.randint(1, 2), decls=r.random() < 0.2))
        c.in_if -= 1
        s = S('if', [cond], blocks)
        if not self.on('wordarm') and word_arm(s):
            del s.b[1:]
        if not self.on('bytearm') and byte_arm(s):
            # keep the assignment, lose the other arm
            del s.b[1:]
        b.stmts.append(s)

    def loop_bound(self, c, b, n):
        """(bound, slack): a constant n, or a protected local set to a value in
        n..n+slack just before the loop."""
        r = self.rng
        if r.random() < 0.5 or not self.on('varbound'):
            return self.small(n, n), 0
        t = self.int_type()
        d = self.new_local(c, 'n', t, self.small(0, 0))
        c.prot.add(d)
        c.fresh()
        base = self.gen_expr(c, 1)
        value = self.mk('bin', [self.mk('bin', [base, self.small(3, 3)], '&'),
                                self.small(n, n)], '+')
        value = self.stored(value, t, 'local')
        b.stmts.append(S('expr', [self.mk('asg', [self.mk('var', a=d), value], '=')]))
        c.ints.append(d)
        return self.mk('var', a=d), 3

    def loop_body(self, c, depth, counter=None, hi=None):
        r = self.rng
        mark = c.mark()
        if counter is not None:
            counter.rng = hi
            c.ints.append(counter)
            c.counters.append(counter)
        c.loop += 1
        outer, c.in_if = c.in_if, 0
        body = self.gen_block(c, depth - 1, r.randint(1, 3), decls=r.random() < 0.3)
        c.in_if = outer
        c.loop -= 1
        c.release(mark)
        return body

    def st_for(self, c, depth, b):
        r = self.rng
        t = self.int_type()
        lo = r.randint(0, 3)
        n = r.randint(1, 5)
        hi = lo + n
        # with `armfordecl` off a `for` declares its counter only when it is
        # directly in the function body, not inside any other statement
        c99 = self.on('c99for') and r.random() < 0.35 \
            and (self.on('armfordecl') or not c.arm)
        down = r.random() < 0.35
        if c99:
            d = Decl(c.name('i'), t, 'block', cname=self.spelled(t))
        else:
            d = self.new_local(c, 'i', t, self.small(0, 0))
        c.prot.add(d)
        var = lambda: self.mk('var', a=d)
        if not down:
            bound, slack = self.loop_bound(c, b, hi)
            start = self.small(lo, lo)
            x = r.random()
            if x < 0.6:
                test = self.mk('bin', [var(), bound], '<')
            elif x < 0.8:
                test = self.mk('bin', [var(), bound], '!=')
            elif bound.op == 'const':
                bound.a -= 1
                test = self.mk('bin', [var(), bound], '<=')
            else:
                test = self.mk('bin', [var(), bound], '<')
            step = r.choice([
                self.mk('incdec', [var()], a=('++', False)),
                self.mk('incdec', [var()], a=('++', True)),
                self.mk('asg', [var(), self.small(1, 1)], '+='),
                self.mk('asg', [var(), self.mk('bin', [var(), self.small(1, 1)], '+')], '=')])
            top = hi - 1 + slack
        else:
            start = self.small(hi, hi)
            x = r.random()
            if x < 0.5 or not t.signed:
                test = self.mk('bin', [var(), self.small(lo, lo)], '>' if x < 0.4 else '!=')
                top = hi
            else:
                test = self.mk('bin', [var(), self.small(lo, lo)], '>=')
                top = hi
            step = r.choice([
                self.mk('incdec', [var()], a=('--', False)),
                self.mk('incdec', [var()], a=('--', True)),
                self.mk('asg', [var(), self.small(1, 1)], '-=')])
        if c99:
            d.init = start
            init = None
        else:
            init = self.mk('asg', [var(), start], '=')
        body = self.loop_body(c, depth, d, top)
        b.stmts.append(S('for', [init, test, step], [body], a=d if c99 else None))

    def counter_type(self):
        """Type for a loop counter stepped inside the loop condition. With
        `nestedwideless` off it is never 32 bits wide."""
        t = self.int_type()
        while t.bits == 32 and not self.on('nestedwideless'):
            t = self.int_type()
        return t

    def st_while(self, c, depth, b):
        r = self.rng
        t = self.counter_type()
        n = r.randint(1, 5)
        d = self.new_local(c, 'w', t, self.small(0, 0))
        c.prot.add(d)
        var = lambda: self.mk('var', a=d)
        if r.random() < 0.5:
            b.stmts.append(S('expr', [self.mk('asg', [var(), self.small(n, n)], '=')]))
            post = self.mk('incdec', [var()], a=('--', False))
            test = self.mk('bin', [post, self.small(0, 0)], '>' if r.random() < 0.5 else '!=')
            body = self.loop_body(c, depth)
        else:
            b.stmts.append(S('expr', [self.mk('asg', [var(), self.small(0, 0)], '=')]))
            bound, _ = self.loop_bound(c, b, n)
            post = self.mk('incdec', [var()], a=('++', False))
            test = self.mk('bin', [post, bound], '<')
            body = self.loop_body(c, depth)
        b.stmts.append(S('while', [test], [body]))

    def st_do(self, c, depth, b):
        r = self.rng
        t = self.counter_type()
        n = r.randint(1, 5)
        d = self.new_local(c, 'd', t, self.small(0, 0))
        c.prot.add(d)
        var = lambda: self.mk('var', a=d)
        b.stmts.append(S('expr', [self.mk('asg', [var(), self.small(0, 0)], '=')]))
        bound, slack = self.loop_bound(c, b, n)
        pre = self.mk('incdec', [var()], a=('++', True))
        test = self.mk('bin', [pre, bound], '<')
        body = self.loop_body(c, depth, d, n - 1 + slack)
        b.stmts.append(S('do', [test], [body]))

    def st_switch(self, c, depth, b):
        r = self.rng
        x = r.random()
        base = self.gen_expr(c, min(depth, 2))
        if x < 0.5:
            mask = r.choice((3, 7, 15))
            ctrl = self.mk('bin', [base, self.small(mask, mask)], '&')
            values = list(range(mask + 1))
        elif x < 0.8:
            m = r.randint(3, 7)
            ctrl = self.mk('bin', [base, self.small(m, m)], '%')
            values = list(range(-(m - 1), m))
        else:
            ctrl = self.mk('cast', [base], t=r.choice((I8, U8)))
            values = [-128, -2, -1, 0, 1, 2, 3, 10, 100, 127, 128, 200, 255]
            values = [v for v in values if ctrl.t.lo <= v <= ctrl.t.hi]
        r.shuffle(values)
        ncase = r.randint(2, min(5, len(values)))
        cases, blocks = [], []
        pos = 0
        for _ in range(ncase):
            k = 1 if r.random() < 0.8 else 2
            labels = values[pos:pos + k]
            pos += k
            if not labels:
                break
            cases.append((labels, r.random() < 0.75))
            blocks.append(self.gen_block(c, depth - 1, r.randint(1, 2)))
        if r.random() < 0.7:
            at = r.randint(0, len(cases))
            cases.insert(at, (None, r.random() < 0.75))
            blocks.insert(at, self.gen_block(c, depth - 1, r.randint(1, 2)))
        b.stmts.append(S('switch', [ctrl], blocks, a=cases))

    def st_block(self, c, depth, b):
        r = self.rng
        b.stmts.append(S('block', b=[self.gen_block(c, depth - 1, r.randint(1, 3),
                                                    decls=r.randint(1, 2))]))

    def jump_cond(self, c):
        """The condition of a conditional jump, or None when only one that
        folds to a constant could be made and `constjump` is off."""
        for _ in range(8):
            cond = self.gen_cond(c, 1)
            if self.on('constjump') or not is_constant(cond):
                return cond
        return None

    def st_brk(self, c, depth, b):
        cond = self.jump_cond(c)
        if cond is None:
            return self.st_assign(c, depth, b)
        inner = Block()
        inner.stmts.append(S(self.rng.choice(('break', 'continue'))))
        b.stmts.append(S('if', [cond], [inner]))

    def returned(self, fn, e):
        """e as the operand of `return` in fn. With `blockret` off a
        nested-block object returned to an 8-bit type goes through a cast."""
        if not self.on('blockret') and block_return(fn.ret, e):
            return self.mk('cast', [e], t=fn.ret)
        return e

    def st_ret(self, c, depth, b):
        if c.fn.ret is None or not is_int(c.fn.ret):
            return
        cond = self.jump_cond(c)
        if cond is None:
            return self.st_assign(c, depth, b)
        inner = Block()
        c.fresh()
        inner.stmts.append(S('ret', [self.returned(c.fn, self.gen_expr(c, min(depth, 2)))]))
        b.stmts.append(S('if', [cond], [inner]))

    def st_goto(self, c, depth, b):
        cond = self.jump_cond(c)
        if cond is None:
            return self.st_assign(c, depth, b)
        c.labels += 1
        # a label name is unique in the program unless `duplabel` is on
        label = 'L%d' % c.labels if self.on('duplabel') else '%s_L%d' % (c.fn.name, c.labels)
        b.stmts.append(S('goto', [cond], a=label))
        for _ in range(self.rng.randint(1, 2)):
            self.st_assign(c, depth - 1, b)
        b.stmts.append(S('label', a=label))

    def st_scopy(self, c, depth, b):
        dst = self.struct_lvalue(c, depth, write=True)
        if dst is None:
            return self.st_assign(c, depth, b)
        st = ty(dst)
        srcs = [f for f in self.struct_fns if f.ret is st]
        args = None
        if srcs and not c.pure and dst.op == 'var' and self.rng.random() < 0.6:
            # the callee may store to globals, so the destination must need no
            # evaluation of its own
            f = self.rng.choice(srcs)
            args = self.call_args(c, f, depth)
        if args is not None:
            src = self.mk('call', args, a=f)
        else:
            src = None
            for _ in range(8):
                cand = self.struct_lvalue(c, depth, write=False)
                if cand is not None and ty(cand) is st:
                    src = cand
                    break
            if src is None:
                return self.st_assign(c, depth, b)
        b.stmts.append(S('expr', [self.mk('asg', [dst, src], '=')]))

    def st_walk(self, c, depth, b):
        r = self.rng
        arrs = [d for d in c.arrs if len(d.dims) == 1 and (not c.pure or d.storage != 'global')
                and (d.t.bits < 32 or self.on('blockptr'))
                and (d.t is not U16 or self.on('blockptr'))]
        if not arrs:
            return self.st_assign(c, depth, b)
        arr = r.choice(arrs)
        blk = Block()
        q = Decl(c.name('q'), PtrType(arr.t), 'block',
                 init=self.mk('addr', [self.small(0, 0)], a=(arr, arr.dims[0])))
        q.tgt_local = arr.storage != 'global'
        end = Decl(c.name('e'), PtrType(arr.t), 'block', init=self.mk('aend', a=arr))
        blk.decls += [q, end]
        c.prot.update((q, end))
        test = self.mk('bin', [self.mk('var', a=q), self.mk('var', a=end)], '!=')
        targets = [d for d in c.ints if c.can_write(d) and d.storage != 'global'
                   and not d.addr_taken]
        if targets and r.random() < 0.5:
            acc = r.choice(targets)
            bump = self.mk('incdec', [self.mk('var', a=q)], a=('++', False))
            op = r.choice(('+=', '^=', '|=', '-='))
            body = Block()
            body.stmts.append(S('expr', [self.mk('asg', [
                self.mk('var', a=acc), self.mk('deref', [bump])], op)]))
            blk.stmts.append(S('while', [test], [body]))
        else:
            mark = c.mark()
            c.walk.append(q)
            c.loop += 1
            body = self.gen_block(c, depth - 1, r.randint(1, 2))
            c.loop -= 1
            c.release(mark)
            step = self.mk('incdec', [self.mk('var', a=q)], a=('++', r.random() < 0.5))
            blk.stmts.append(S('for', [None, test, step], [body]))
        b.stmts.append(S('block', b=[blk]))

    def st_ptr(self, c, depth, b):
        """Write through a pointer."""
        r = self.rng
        ptrs = [d for d in c.ptrs if c.can_write(d)]
        sptrs = [d for d in c.sptrs if c.can_write(d)]
        if ptrs and (not sptrs or r.random() < 0.6):
            d = r.choice(ptrs)
            if r.random() < 0.5:
                lv = self.mk('deref', [self.mk('var', a=d)])
            else:
                lv = self.mk('pidx', [self.gen_index(c, depth, d.valid)], a=d)
        elif sptrs:
            d = r.choice(sptrs)
            lv = self.field_of(c, depth, self.mk('deref', [self.mk('var', a=d)]), d.t.to)
        else:
            return self.st_assign(c, depth, b)
        op = r.choice(('=', '=', '+=', '^=', '-='))
        b.stmts.append(S('expr', [self.compound(lv, self.gen_expr(c, depth), op)]))

    def call_args(self, c, f, depth):
        args = []
        for p in f.params:
            arg = None
            if isinstance(p.t, PtrType):
                arg = self.ptr_arg(c, p)
            elif isinstance(p.t, StructType):
                for _ in range(8):
                    cand = self.struct_lvalue(c, depth, write=False)
                    if cand is not None and ty(cand) is p.t:
                        arg = cand
                        break
            else:
                arg = self.gen_expr(c, min(depth, 2))
            if arg is None:
                return None
            args.append(arg)
        return args

    def ptr_arg(self, c, p, constant=False):
        """A pointer to global storage with at least p.valid elements."""
        r = self.rng
        to = p.t.to
        index = (lambda n: self.small(0, n - 1)) if constant \
            else (lambda n: self.gen_index(c, 1, n))
        if isinstance(to, StructType):
            opts = [d for d in c.svars if d.t is to and d.storage == 'global']
            arrs = [d for d in c.sarrs if d.t is to and d.storage == 'global']
            if arrs and (not opts or r.random() < 0.5):
                d = r.choice(arrs)
                return self.mk('addr', [index(d.dims[0])], a=(d, 1))
            if opts:
                return self.mk('addrof', a=r.choice(opts))
            return None
        arrs = [d for d in c.arrs if d.t is to and len(d.dims) == 1 and d.dims[0] >= p.valid
                and d.storage == 'global']
        if not arrs:
            return None
        d = r.choice(arrs)
        return self.mk('addr', [index(d.dims[0] - p.valid + 1)], a=(d, p.valid))

    def st_call(self, c, depth, b):
        r = self.rng
        f = r.choice(self.impure_fns)
        args = self.call_args(c, f, depth)
        if args is None:
            return self.st_assign(c, depth, b)
        call = self.mk('call', args, a=f)
        if f.ret is None or not is_int(f.ret):
            if f.ret is None:
                b.stmts.append(S('expr', [call]))
            else:
                dsts = [d for d in c.svars if d.t is f.ret and c.can_write(d)]
                if not dsts:
                    return self.st_assign(c, depth, b)
                b.stmts.append(S('expr', [self.mk('asg', [self.mk('var', a=r.choice(dsts)),
                                                         call], '=')]))
            return
        simple = [d for d in c.ints if c.can_write(d) and d not in c.used]
        local = [d for d in simple if d.storage != 'global' and not d.addr_taken]
        x = r.random()
        if x < 0.5 and simple:
            dst = r.choice(simple)
            b.stmts.append(S('expr', [self.mk('asg', [
                self.mk('var', a=dst), self.stored(call, dst.t, dst.storage)], '=')]))
        elif x < 0.7 and local:
            op = r.choice(('+=', '^=', '-=', '|='))
            b.stmts.append(S('expr', [self.mk('asg', [self.mk('var', a=r.choice(local)),
                                                     call], op)]))
        elif x < 0.9:
            other = self.mk('var', a=r.choice(local)) if local and r.random() < 0.5 \
                else self.const()
            cond = self.mk('bin', [call, other], r.choice(CMP))
            c.in_if += 1
            then = self.gen_block(c, depth - 1, r.randint(1, 2))
            c.in_if -= 1
            b.stmts.append(S('if', [cond], [then]))
        else:
            b.stmts.append(S('expr', [call]))

    # ---- functions

    def locals_for(self, c, fn, n):
        r = self.rng
        for _ in range(n):
            t = self.int_type()
            c.fresh()
            init, later = self.initialiser(c, t, self.gen_expr(c, 1), 'local')
            d = self.new_local(c, 'l', t, init)
            # a pure helper keeps no state, so calls to it can be reordered
            d.static = not c.pure and self.on('static') and r.random() < 0.06
            if d.static:
                d.init = self.value(t)
            else:
                self.assign_after(fn.body, d, later)
            c.ints.append(d)
        if r.random() < 0.5 and self.on('localarr'):
            t = self.int_type()
            n = r.choice((2, 4, 8))
            d = Decl(c.name('v'), t, 'local', dims=(n,), init=self.init_value(t, (n,)))
            fn.body.decls.append(d)
            c.arrs.append(d)
            if r.random() < 0.6 and self.on('ptr'):
                valid = r.choice((1, 2)) if n == 2 else r.choice((1, 2, 4))
                p = Decl(c.name('p'), PtrType(t), 'local',
                         init=self.mk('addr', [self.small(0, n - valid)], a=(d, valid)))
                p.valid, p.tgt_local = valid, True
                c.prot.add(p)
                fn.body.decls.append(p)
                c.ptrs.append(p)
        if r.random() < 0.35 and self.on('struct'):
            st = r.choice(self.prog.structs)
            d = Decl(c.name('t'), st, 'local', init=self.init_value(st, ()))
            fn.body.decls.append(d)
            c.svars.append(d)
        if not c.pure and r.random() < 0.5 and self.on('ptr'):
            arrs = [g for g in self.prog.globals if g.dims and len(g.dims) == 1
                    and is_int(g.t)]
            g = r.choice(arrs)
            valid = r.choice((1, 2, 4))
            p = Decl(c.name('p'), PtrType(g.t), 'local',
                     init=self.mk('addr', [self.gen_index(c, 1, g.dims[0] - valid + 1)],
                                  a=(g, valid)))
            p.valid = valid
            c.prot.add(p)
            fn.body.decls.append(p)
            c.ptrs.append(p)

    def params_for(self, c, fn, n, pointers):
        r = self.rng
        targets = sorted({g.t for g in self.prog.globals
                          if is_int(g.t) and len(g.dims) == 1 and g.dims[0] >= 4},
                         key=lambda t: t.name)
        ptr = pointers and self.on('ptr')
        sptr = ptr and self.on('struct')
        sval = pointers and self.on('struct') and self.on('structval')
        for i in range(n):
            x = r.random()
            if ptr and x < 0.12:
                t = r.choice(targets)
                d = Decl('x%d' % i, PtrType(t), 'param')
                d.valid = r.choice((1, 2, 4))
                c.prot.add(d)
                c.ptrs.append(d)
            elif sptr and 0.12 <= x < 0.2:
                d = Decl('x%d' % i, PtrType(r.choice(self.prog.structs)), 'param')
                c.prot.add(d)
                c.sptrs.append(d)
            elif sval and 0.2 <= x < 0.27:
                st = r.choice(self.prog.structs)
                d = Decl('x%d' % i, st, 'param')
                c.svars.append(d)
            else:
                t = self.int_type()
                d = Decl('x%d' % i, t, 'param', cname=self.spelled(t))
                if t.bits == 32 and not self.on('widehelperparam'):
                    # with this off a 32-bit parameter is only read through a
                    # local copy (BUG-020 needs the parameter itself)
                    copy_ = Decl('y%d' % i, t, 'local', init=self.mk('var', a=d),
                                 cname=self.spelled(t))
                    fn.body.decls.append(copy_)
                    c.ints.append(copy_)
                else:
                    c.ints.append(d)
            fn.params.append(d)

    def finish(self, c, fn):
        if fn.ret is not None and is_int(fn.ret):
            c.fresh()
            fn.body.stmts.append(S('ret', [self.gen_expr(c, 2)]))

    def gen_pure(self, n):
        r = self.rng
        fn = Func('h%d' % n, self.int_type(), [], 'pure')
        c = self.ctx(fn, True)
        self.params_for(c, fn, r.randint(1, 4), False)
        self.locals_for(c, fn, r.randint(0, 2))
        for _ in range(r.randint(0, 3)):
            self.gen_stmt(c, 1, fn.body)
        self.finish(c, fn)
        fn.via_ptr = self.on('fnptr') and r.random() < 0.25
        return fn

    def gen_rec(self, n):
        """Bounded recursion: depth is at most 7 whatever the argument."""
        r = self.rng
        t = self.int_type()
        fn = Func('r%d' % n, t, [], 'rec')
        c = self.ctx(fn, True)
        cnt = Decl('x0', U8, 'param')
        acc = Decl('x1', t, 'param', cname=self.spelled(t))
        fn.params += [cnt, acc]
        c.ints += [cnt, acc]
        c.prot.add(cnt)
        var = lambda d: self.mk('var', a=d)
        low = lambda: self.mk('bin', [var(cnt), self.small(7, 7)], '&')
        stop = Block()
        stop.stmts.append(S('ret', [var(acc)]))
        fn.body.stmts.append(S('if', [self.mk('bin', [low(), self.small(0, 0)], '==')], [stop]))
        c.fresh()
        nxt = self.mk('cast', [self.mk('bin', [low(), self.small(1, 1)], '-')], t=U8)
        fn.body.stmts.append(S('ret', [self.mk('call', [nxt, self.gen_expr(c, 2)], a=fn)]))
        return fn

    def gen_impure(self, n):
        r = self.rng
        x = r.random()
        if x < 0.15:
            ret = None
        elif x < 0.3 and self.on('struct') and self.on('structval'):
            ret = r.choice(self.prog.structs)
        else:
            ret = self.int_type()
        fn = Func('k%d' % n, ret, [], 'impure')
        c = self.ctx(fn, False)
        self.params_for(c, fn, r.randint(1, 4), True)
        self.locals_for(c, fn, r.randint(0, 2))
        for _ in range(r.randint(1, 4)):
            self.gen_stmt(c, 1, fn.body)
        if ret is not None and not is_int(ret):
            srcs = [d for d in c.svars if d.t is ret]
            if not srcs:
                d = Decl(c.name('t'), ret, 'local', init=self.init_value(ret, ()))
                fn.body.decls.append(d)
                srcs = [d]
            fn.body.stmts.append(S('ret', [self.mk('var', a=r.choice(srcs))]))
        else:
            self.finish(c, fn)
        return fn

    def gen_test(self, n):
        r = self.rng
        fn = Func('f%d' % n, self.int_type(), [], 'test')
        fn.static = r.random() < 0.7
        c = self.ctx(fn, False)
        self.params_for(c, fn, r.randint(0, 5), True)
        self.locals_for(c, fn, r.randint(2, 6))
        for _ in range(r.randint(max(2, self.size // 2), self.size)):
            self.gen_stmt(c, self.depth, fn.body)
        self.finish(c, fn)
        for _ in range(3):
            args = []
            for p in fn.params:
                if isinstance(p.t, PtrType):
                    args.append(self.ptr_arg(c, p, constant=True))
                elif isinstance(p.t, StructType):
                    cands = [g for g in self.prog.globals if g.t is p.t]
                    g = r.choice(cands)
                    if g.dims:
                        args.append(self.mk('idx', [self.small(0, g.dims[0] - 1)], a=g))
                    else:
                        args.append(self.mk('var', a=g))
                else:
                    t = p.t if p.t.bits >= 16 else (I16 if p.t.signed else U16)
                    v = p.t.wrap(self.value(p.t))
                    args.append(self.mk('const', a=t.wrap(v), t=t))
            fn.inputs.append(args)
        return fn

    def generate(self):
        r = self.rng
        self.gen_structs()
        self.gen_globals()
        funcs = self.prog.funcs
        for n in range(r.randint(2, 4)):
            fn = self.gen_pure(n) if n or r.random() < 0.6 or not self.on('rec') \
                else self.gen_rec(n)
            funcs.append(fn)
            self.pure_fns.append(fn)
        for n in range(r.randint(1, 3)):
            fn = self.gen_impure(n)
            funcs.append(fn)
            (self.struct_fns if isinstance(fn.ret, StructType) else self.impure_fns).append(fn)
            if isinstance(fn.ret, StructType):
                self.impure_fns.append(fn)
        for n in range(self.n_tests):
            funcs.append(self.gen_test(n))
        return self.prog


def generate(seed, tests=6, size=10, depth=3, off=frozenset()):
    return Gen(seed, tests, size, depth, off).generate()


def render(prog, checked=False, title=''):
    return Renderer(checked, prog.off).program(prog, title)
