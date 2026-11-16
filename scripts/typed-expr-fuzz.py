#!/usr/bin/env python3
"""Differential fuzzer for dcc's integer expression code generation.

Each generated program is a set of small functions, one per case. A case
applies one operator to two operands of chosen integer types and storage
(parameter, local, nested-block local, global, static local, array element,
struct member, pointer, constant) and consumes the result in a chosen context
(return, if, if-not, ternary, compound assignment, converted argument, global
store, loop accumulation). The expected values come from a model of C's
conversions with dcc's sizes (char 8, short and int 16, long 32 bits), so no
host compiler is involved; inputs that would be undefined are skipped.

  typed-expr-fuzz.py --seeds 1-200            fuzz 200 programs, peep and nopeep
  typed-expr-fuzz.py --seeds 1-200 --mode tree   nested expression trees instead
  typed-expr-fuzz.py --seeds 1-200 --mode store  stored values of updates instead
  typed-expr-fuzz.py --seeds 1-200 --mode prog   small programs with aliases and calls
  typed-expr-fuzz.py --seeds 1-200 --mode scope  prog plus blocks, switch, goto, byte access
  typed-expr-fuzz.py --seeds 7 --keep         keep seed 7's program and outputs
  typed-expr-fuzz.py --generate 7 prog.c prog.expected

Failing seeds keep their directory under --work (default build/typed-expr-fuzz)
holding t.c, t.expected and each mode's output; each output line is
"<case*10+input in hex> <result as unsigned long in hex>".
"""
from __future__ import annotations

import argparse
import concurrent.futures
import os
import random
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

TYPES = {
    'signed char': (8, True, 1), 'unsigned char': (8, False, 1),
    'short': (16, True, 2), 'unsigned short': (16, False, 2),
    'int': (16, True, 3), 'unsigned': (16, False, 3),
    'long': (32, True, 4), 'unsigned long': (32, False, 4),
}
NAMES = list(TYPES)
UNSIGNED_OF = {'int': 'unsigned', 'long': 'unsigned long', 'short': 'unsigned short',
               'signed char': 'unsigned char'}
COMPARISONS = ('<', '>', '<=', '>=', '==', '!=')
OPS = ['+', '-', '*', '/', '%', '&', '|', '^', '<<', '>>', *COMPARISONS, '&&', '||']
SHIFT_COUNT_TYPES = ['int', 'unsigned char', 'unsigned', 'signed char', 'long', 'unsigned long']
STORES = ['param', 'local', 'block', 'global', 'static', 'array', 'member', 'ptr', 'const']
CONTEXTS = ['return', 'if', 'ifnot', 'compound', 'arg', 'global', 'ternary', 'loop']
INPUTS = 4


def bits(t): return TYPES[t][0]
def signed(t): return TYPES[t][1]
def rank(t): return TYPES[t][2]
def lo(t): return -(1 << (bits(t) - 1)) if signed(t) else 0
def hi(t): return (1 << (bits(t) - 1)) - 1 if signed(t) else (1 << bits(t)) - 1


def wrap(v, t):
    m = 1 << bits(t)
    v %= m
    if signed(t) and v >= m // 2:
        v -= m
    return v


def promote(t):
    if rank(t) < 3:
        return 'int' if lo(t) >= lo('int') and hi(t) <= hi('int') else 'unsigned'
    return t


def common(a, b):
    a, b = promote(a), promote(b)
    if a == b:
        return a
    if signed(a) == signed(b):
        return a if rank(a) > rank(b) else b
    u, s = (a, b) if not signed(a) else (b, a)
    if rank(u) >= rank(s):
        return u
    if bits(s) > bits(u):
        return s
    return UNSIGNED_OF[s]


class Undefined(Exception):
    pass


def binop(op, a, ta, b, tb):
    """(value, type) of a OP b in C, or Undefined."""
    if op in ('<<', '>>'):
        rt = promote(ta)
        va, vb = wrap(a, rt), wrap(b, promote(tb))
        if vb < 0 or vb >= bits(rt):
            raise Undefined
        if op == '<<':
            if signed(rt) and (va < 0 or (va << vb) > hi(rt)):
                raise Undefined
            return wrap(va << vb, rt), rt
        if va < 0:
            raise Undefined  # implementation-defined
        return va >> vb, rt
    if op in ('&&', '||'):
        r = (a != 0 and b != 0) if op == '&&' else (a != 0 or b != 0)
        return int(r), 'int'
    ct = common(ta, tb)
    va, vb = wrap(a, ct), wrap(b, ct)
    if op in COMPARISONS:
        r = {'<': va < vb, '>': va > vb, '<=': va <= vb, '>=': va >= vb,
             '==': va == vb, '!=': va != vb}[op]
        return int(r), 'int'
    if op in ('+', '-', '*'):
        r = {'+': va + vb, '-': va - vb, '*': va * vb}[op]
        if signed(ct) and not lo(ct) <= r <= hi(ct):
            raise Undefined
        return wrap(r, ct), ct
    if op in ('/', '%'):
        if vb == 0 or (signed(ct) and va == lo(ct) and vb == -1):
            raise Undefined
        q = abs(va) // abs(vb)
        if (va < 0) != (vb < 0):
            q = -q
        return (q if op == '/' else va - q * vb), ct
    m = (1 << bits(ct)) - 1
    r = {'&': (va & m) & (vb & m), '|': (va & m) | (vb & m), '^': (va & m) ^ (vb & m)}[op]
    return wrap(r, ct), ct


def literal(v, t):
    """A C expression of type t with value v."""
    if t == 'int' and v > lo('int'):
        return str(v) if v >= 0 else '(%d)' % v
    if t == 'unsigned':
        return '%dU' % v
    if t == 'long' and v > lo('long'):
        return '%dL' % v if v >= 0 else '(%dL)' % v
    if t == 'unsigned long':
        return '%dUL' % v
    return '((%s)%dL)' % (t, v)


def interesting(t, rng):
    pool = [0, 1, -1, 2, 3, 7, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff,
            0x10000, 0x7fffffff, lo(t), hi(t), lo(t) + 1, hi(t) - 1, 100, -100,
            rng.randint(-70000, 70000), rng.randint(0, 255), rng.randint(-128, 127)]
    return wrap(rng.choice(pool), t)


class Case:
    def __init__(self, n, rng):
        self.n = n
        self.op = rng.choice(OPS)
        self.ta, self.tb = rng.choice(NAMES), rng.choice(NAMES)
        if self.op in ('<<', '>>'):
            self.tb = rng.choice(SHIFT_COUNT_TYPES)
        self.sa, self.sb = rng.choice(STORES), rng.choice(STORES)
        if self.sa == 'const' and self.sb == 'const':
            self.sb = 'param'
        self.rt = rng.choice(NAMES)
        self.ctx = rng.choice(CONTEXTS)
        if self.ctx == 'compound' and self.op in COMPARISONS + ('&&', '||'):
            self.ctx = 'return'
        self.ca = interesting(self.ta, rng)
        self.cb = interesting(self.tb, rng)
        if self.op in ('<<', '>>') and self.sb == 'const':
            self.cb = rng.randint(0, 9)
        self.inputs = []
        for _ in range(60):
            if len(self.inputs) == INPUTS:
                break
            a = self.ca if self.sa == 'const' else interesting(self.ta, rng)
            b = self.cb if self.sb == 'const' else interesting(self.tb, rng)
            if self.op in ('<<', '>>') and self.sb != 'const' and rng.random() < 0.7:
                b = rng.randint(0, 17)
            try:
                self.expected(a, b)
            except Undefined:
                continue
            self.inputs.append((a, b))

    def expected(self, a, b):
        v, _ = binop(self.op, a, self.ta, b, self.tb)
        if self.ctx == 'compound':
            return wrap(wrap(v, self.ta), self.rt)
        if self.ctx == 'if':
            return wrap(1 if v else 0, self.rt)
        if self.ctx == 'ifnot':
            return wrap(7 if not v else 3, self.rt)
        if self.ctx == 'ternary':
            return wrap(5 if v else -9, self.rt)
        if self.ctx == 'loop':
            # acc += (rt)e: the sum is in rt's promoted type, then converted back.
            x, pt, acc = wrap(v, self.rt), promote(self.rt), 0
            for _ in range(3):
                total = acc + x
                if signed(pt) and not lo(pt) <= total <= hi(pt):
                    raise Undefined
                acc = wrap(total, self.rt)
            return acc
        return wrap(v, self.rt)

    def expected_for(self, k):
        return self.expected(*self.inputs[k])

    def operand(self, which):
        s, t, c = (self.sa, self.ta, self.ca) if which == 'a' else (self.sb, self.tb, self.cb)
        n = self.n
        return {'param': which, 'local': 'l' + which, 'block': 'k' + which,
                'global': 'g%d%s' % (n, which), 'static': 's' + which,
                'array': 'r%d%s[i]' % (n, which), 'member': 'm%d.%s' % (n, which),
                'ptr': '(*p%s)' % which, 'const': literal(c, t)}[s]

    def emit(self, out):
        n, ta, tb, rt = self.n, self.ta, self.tb, self.rt
        for which, s, t in (('a', self.sa, ta), ('b', self.sb, tb)):
            if s == 'global':
                out.append('%s g%d%s;' % (t, n, which))
            elif s == 'array':
                out.append('%s r%d%s[3];' % (t, n, which))
        if 'member' in (self.sa, self.sb):
            out.append('struct { char pad; %s a; %s b; } m%d;' % (ta, tb, n))
        if self.ctx == 'global':
            out.append('%s o%d;' % (rt, n))
        params = ['%s a' % ta, '%s b' % tb, 'int i', '%s *pa' % ta, '%s *pb' % tb]
        out.append('%s t%d(%s)' % (rt, n, ', '.join(params)))
        out.append('{')
        for which, s, t in (('a', self.sa, ta), ('b', self.sb, tb)):
            if s == 'local':
                out.append('    %s l%s;' % (t, which))
            if s == 'static':
                out.append('    static %s s%s;' % (t, which))
        for which, s in (('a', self.sa), ('b', self.sb)):
            if s in ('local', 'static'):
                out.append('    %s%s = %s;' % (s[0], which, which))
        a, b = self.operand('a'), self.operand('b')
        e = '%s %s %s' % (a, self.op, b)
        body = {
            'return': ['return (%s)(%s);' % (rt, e)],
            'if': ['if (%s) return 1;' % e, 'return 0;'],
            'ifnot': ['if (!(%s)) return 7;' % e, 'return 3;'],
            'ternary': ['return (%s)((%s) ? 5 : -9);' % (rt, e)],
            'compound': ['%s x;' % ta, 'x = %s;' % a, 'x %s= %s;' % (self.op, b),
                         'return (%s)x;' % rt],
            'arg': ['return take%d(%s);' % (n, e)],
            'global': ['o%d = %s;' % (n, e), 'return o%d;' % n],
            'loop': ['%s acc;' % rt, 'int k;', 'acc = 0;', 'for (k = 0; k < 3; ++k)',
                     '    acc += (%s)(%s);' % (rt, e), 'return acc;'],
        }[self.ctx]
        # The body opens its own block so its declarations (C89) follow the
        # operand setup above; block-scoped operands are declared there too.
        out.append('    {')
        for which, s, t in (('a', self.sa, ta), ('b', self.sb, tb)):
            if s == 'block':
                out.append('        %s k%s = %s;' % (t, which, which))
        out.extend('        ' + line for line in body)
        out.append('    }')
        out.append('}')
        out.append('')

    def emit_take(self, out):
        if self.ctx == 'arg':
            out.append('%s take%d(%s v) { return v; }' % (self.rt, self.n, self.rt))

    def emit_calls(self, out):
        n = self.n
        for k, (a, b) in enumerate(self.inputs):
            setup = []
            for which, s, t, v in (('a', self.sa, self.ta, a), ('b', self.sb, self.tb, b)):
                if s == 'global':
                    setup.append('g%d%s = %s;' % (n, which, literal(v, t)))
                elif s == 'array':
                    setup.append('r%d%s[%d] = %s;' % (n, which, k % 3, literal(v, t)))
                elif s == 'member':
                    setup.append('m%d.%s = %s;' % (n, which, literal(v, t)))
                elif s == 'ptr':
                    setup.append('v%s = %s;' % (which, literal(v, t)))
            if 'ptr' in (self.sa, self.sb):
                out.append('    { %s va; %s vb;' % (self.ta, self.tb))
            else:
                out.append('    {')
            out.extend('      ' + line for line in setup)
            pa = '&va' if self.sa == 'ptr' else '0'
            pb = '&vb' if self.sb == 'ptr' else '0'
            out.append('      show(%d, (unsigned long)t%d(%s, %s, %d, %s, %s)); }' % (
                n * 10 + k, n, literal(a, self.ta), literal(b, self.tb), k % 3, pa, pb))


COMPOUND_OPS = ['+', '-', '*', '/', '%', '&', '|', '^', '<<', '>>']
LVALUES = ['global', 'array', 'member', 'ptr', 'static', 'local', 'block']
STORE_CONTEXTS = ['cmem', 'cvalue', 'preinc', 'predec', 'postinc', 'postdec']


class StoreCase:
    """One update of an lvalue of a chosen type and storage: a compound
    assignment (its stored value, or the assignment's own value) or a pre/post
    increment or decrement."""

    def __init__(self, n, rng):
        self.n = n
        self.ctx = rng.choice(STORE_CONTEXTS)
        self.op = rng.choice(COMPOUND_OPS)
        self.ta = rng.choice(NAMES)
        self.tb = rng.choice(SHIFT_COUNT_TYPES if self.op in ('<<', '>>') else NAMES)
        self.ls = rng.choice(LVALUES)
        self.sb = rng.choice(STORES)
        self.rt = rng.choice(NAMES)
        self.cb = rng.randint(0, 9) if self.op in ('<<', '>>') else interesting(self.tb, rng)
        self.inputs = []
        for _ in range(60):
            if len(self.inputs) == INPUTS:
                break
            a = interesting(self.ta, rng)
            b = self.cb if self.sb == 'const' else interesting(self.tb, rng)
            if self.op in ('<<', '>>') and self.sb != 'const' and rng.random() < 0.7:
                b = rng.randint(0, 17)
            try:
                self.expected(a, b)
            except Undefined:
                continue
            self.inputs.append((a, b))

    def step(self, a, delta):
        """The lvalue after ++ or --: computed in the promoted type, then
        converted back."""
        pt = promote(self.ta)
        v = wrap(a, pt) + delta
        if signed(pt) and not lo(pt) <= v <= hi(pt):
            raise Undefined
        return wrap(v, self.ta)

    def expected(self, a, b):
        a = wrap(a, self.ta)
        if self.ctx in ('cmem', 'cvalue'):
            v, _ = binop(self.op, a, self.ta, b, self.tb)
            return wrap(wrap(v, self.ta), self.rt)
        if self.ctx in ('preinc', 'predec'):
            return wrap(self.step(a, 1 if self.ctx == 'preinc' else -1), self.rt)
        after = self.step(a, 1 if self.ctx == 'postinc' else -1)
        v, _ = binop('^', a, self.ta, after, self.ta)  # old value ^ new value
        return wrap(v, self.rt)

    def expected_for(self, k):
        return self.expected(*self.inputs[k])

    def lvalue(self):
        n = self.n
        return {'global': 'h%d' % n, 'array': 'q%d[i]' % n, 'member': 'mm%d.c' % n,
                'ptr': '(*pa)', 'static': 'sl', 'local': 'll', 'block': 'kl'}[self.ls]

    def operand_b(self):
        n = self.n
        return {'param': 'b', 'local': 'lb', 'block': 'kb', 'global': 'g%db' % n,
                'static': 'sb', 'array': 'r%db[i]' % n, 'member': 'm%d.b' % n,
                'ptr': '(*pb)', 'const': literal(self.cb, self.tb)}[self.sb]

    def emit(self, out):
        n, ta, tb, rt = self.n, self.ta, self.tb, self.rt
        if self.ls == 'global':
            out.append('%s h%d;' % (ta, n))
        elif self.ls == 'array':
            out.append('%s q%d[3];' % (ta, n))
        elif self.ls == 'member':
            out.append('struct { char pad; %s c; } mm%d;' % (ta, n))
        if self.sb == 'global':
            out.append('%s g%db;' % (tb, n))
        elif self.sb == 'array':
            out.append('%s r%db[3];' % (tb, n))
        elif self.sb == 'member':
            out.append('struct { char pad; %s b; } m%d;' % (tb, n))
        out.append('%s t%d(%s a, %s b, int i, %s *pa, %s *pb)' % (rt, n, ta, tb, ta, tb))
        out.append('{')
        if self.ls == 'local':
            out.append('    %s ll;' % ta)
        elif self.ls == 'static':
            out.append('    static %s sl;' % ta)
        if self.sb == 'local':
            out.append('    %s lb;' % tb)
        elif self.sb == 'static':
            out.append('    static %s sb;' % tb)
        if self.sb in ('local', 'static'):
            out.append('    %sb = b;' % self.sb[0])
        out.append('    {')
        if self.ls == 'block':
            out.append('        %s kl;' % ta)
        if self.sb == 'block':
            out.append('        %s kb = b;' % tb)
        if self.ctx in ('postinc', 'postdec'):
            out.append('        %s v;' % ta)
        lv, b = self.lvalue(), self.operand_b()
        out.append('        %s = a;' % lv)
        out.extend('        ' + line for line in {
            'cmem': ['%s %s= %s;' % (lv, self.op, b), 'return (%s)%s;' % (rt, lv)],
            'cvalue': ['return (%s)(%s %s= %s);' % (rt, lv, self.op, b)],
            'preinc': ['return (%s)(++%s);' % (rt, lv)],
            'predec': ['return (%s)(--%s);' % (rt, lv)],
            'postinc': ['v = %s++;' % lv, 'return (%s)(v ^ %s);' % (rt, lv)],
            'postdec': ['v = %s--;' % lv, 'return (%s)(v ^ %s);' % (rt, lv)],
        }[self.ctx])
        out.append('    }')
        out.append('}')
        out.append('')

    def emit_take(self, out):
        pass

    def emit_calls(self, out):
        n = self.n
        for k, (a, b) in enumerate(self.inputs):
            setup = []
            if self.sb == 'global':
                setup.append('g%db = %s;' % (n, literal(b, self.tb)))
            elif self.sb == 'array':
                setup.append('r%db[%d] = %s;' % (n, k % 3, literal(b, self.tb)))
            elif self.sb == 'member':
                setup.append('m%d.b = %s;' % (n, literal(b, self.tb)))
            elif self.sb == 'ptr':
                setup.append('vb = %s;' % literal(b, self.tb))
            out.append('    { %s va; %s vb;' % (self.ta, self.tb))
            out.extend('      ' + line for line in setup)
            pa = '&va' if self.ls == 'ptr' else '0'
            pb = '&vb' if self.sb == 'ptr' else '0'
            out.append('      show(%d, (unsigned long)t%d(%s, %s, %d, %s, %s)); }' % (
                n * 10 + k, n, literal(a, self.ta), literal(b, self.tb), k % 3, pa, pb))


TREE_LEAVES = 3
TREE_STORES = ['param', 'local', 'block', 'global', 'static', 'array', 'member', 'ptr', 'const']
TREE_CONTEXTS = ['return', 'if', 'arg', 'global']


class TreeCase:
    """One expression tree over three typed leaves: binary operators, unary
    - ~ !, casts and ?:, nested up to depth 3."""

    def __init__(self, n, rng):
        self.n = n
        self.types = [rng.choice(NAMES) for _ in range(TREE_LEAVES)]
        self.stores = [rng.choice(TREE_STORES) for _ in range(TREE_LEAVES)]
        if all(s == 'const' for s in self.stores):
            self.stores[0] = 'param'
        self.consts = [interesting(t, rng) for t in self.types]
        self.rt = rng.choice(NAMES)
        self.ctx = rng.choice(TREE_CONTEXTS)
        self.tree = self.grow(rng, rng.randint(2, 3))
        self.inputs = []
        for _ in range(80):
            if len(self.inputs) == INPUTS:
                break
            values = tuple(self.consts[i] if self.stores[i] == 'const'
                           else interesting(self.types[i], rng) for i in range(TREE_LEAVES))
            try:
                self.expected(values)
            except Undefined:
                continue
            self.inputs.append(values)

    def grow(self, rng, depth):
        if depth == 0 or rng.random() < 0.25:
            return ('leaf', rng.randrange(TREE_LEAVES))
        kind = rng.choice(['binary'] * 5 + ['unary', 'cast', 'ternary'])
        if kind == 'binary':
            op = rng.choice(OPS)
            right = self.grow(rng, depth - 1)
            if op in ('<<', '>>'):
                right = ('cast', 'int', ('cast', 'unsigned char', right)) if rng.random() < 0.5 \
                    else ('const', rng.randint(0, 9), rng.choice(SHIFT_COUNT_TYPES))
            return ('binary', op, self.grow(rng, depth - 1), right)
        if kind == 'unary':
            return ('unary', rng.choice('-~!'), self.grow(rng, depth - 1))
        if kind == 'cast':
            return ('cast', rng.choice(NAMES), self.grow(rng, depth - 1))
        return ('ternary', self.grow(rng, depth - 1), self.grow(rng, depth - 1),
                self.grow(rng, depth - 1))

    def type_of(self, node):
        kind = node[0]
        if kind == 'leaf':
            return self.types[node[1]]
        if kind == 'const':
            return node[2]
        if kind == 'binary':
            op, ta, tb = node[1], self.type_of(node[2]), self.type_of(node[3])
            if op in ('<<', '>>'):
                return promote(ta)
            if op in COMPARISONS + ('&&', '||'):
                return 'int'
            return common(ta, tb)
        if kind == 'unary':
            return 'int' if node[1] == '!' else promote(self.type_of(node[2]))
        if kind == 'cast':
            return node[1]
        return common(self.type_of(node[2]), self.type_of(node[3]))

    def evaluate(self, node, values):
        """(value, type); only the arms and operands C evaluates are evaluated."""
        kind = node[0]
        if kind == 'leaf':
            return values[node[1]], self.types[node[1]]
        if kind == 'const':
            return wrap(node[1], node[2]), node[2]
        if kind == 'binary':
            op = node[1]
            a, ta = self.evaluate(node[2], values)
            if op in ('&&', '||') and (a != 0) == (op == '||'):
                return int(op == '||'), 'int'
            b, tb = self.evaluate(node[3], values)
            return binop(op, a, ta, b, tb)
        if kind == 'unary':
            v, t = self.evaluate(node[2], values)
            if node[1] == '!':
                return int(v == 0), 'int'
            pt = promote(t)
            v = wrap(v, pt)
            if node[1] == '~':
                return wrap(~v, pt), pt
            if signed(pt) and not lo(pt) <= -v <= hi(pt):
                raise Undefined
            return wrap(-v, pt), pt
        if kind == 'cast':
            v, _ = self.evaluate(node[2], values)
            return wrap(v, node[1]), node[1]
        c, _ = self.evaluate(node[1], values)
        rt = self.type_of(node)
        v, _ = self.evaluate(node[2] if c else node[3], values)
        return wrap(v, rt), rt

    def expected(self, values):
        v, _ = self.evaluate(self.tree, values)
        if self.ctx == 'if':
            return wrap(1 if v else 0, self.rt)
        return wrap(v, self.rt)

    def leaf_text(self, i):
        n, s, t = self.n, self.stores[i], self.types[i]
        return {'param': 'a%d' % i, 'local': 'l%d' % i, 'block': 'k%d' % i,
                'global': 'g%d_%d' % (n, i), 'static': 's%d' % i,
                'array': 'r%d_%d[i]' % (n, i), 'member': 'm%d.f%d' % (n, i),
                'ptr': '(*p%d)' % i, 'const': literal(self.consts[i], t)}[s]

    def text(self, node):
        kind = node[0]
        if kind == 'leaf':
            return self.leaf_text(node[1])
        if kind == 'const':
            return literal(node[1], node[2])
        if kind == 'binary':
            return '(%s %s %s)' % (self.text(node[2]), node[1], self.text(node[3]))
        if kind == 'unary':
            return '(%s(%s))' % (node[1], self.text(node[2]))
        if kind == 'cast':
            return '((%s)(%s))' % (node[1], self.text(node[2]))
        return '((%s) ? (%s) : (%s))' % (self.text(node[1]), self.text(node[2]),
                                         self.text(node[3]))

    def emit(self, out):
        n, rt = self.n, self.rt
        for i, (s, t) in enumerate(zip(self.stores, self.types)):
            if s == 'global':
                out.append('%s g%d_%d;' % (t, n, i))
            elif s == 'array':
                out.append('%s r%d_%d[3];' % (t, n, i))
        if 'member' in self.stores:
            out.append('struct { char pad; %s } m%d;' % (
                ' '.join('%s f%d;' % (t, i) for i, t in enumerate(self.types)), n))
        if self.ctx == 'global':
            out.append('%s o%d;' % (rt, n))
        params = ['%s a%d' % (t, i) for i, t in enumerate(self.types)] + ['int i'] + \
                 ['%s *p%d' % (t, i) for i, t in enumerate(self.types)]
        out.append('%s t%d(%s)' % (rt, n, ', '.join(params)))
        out.append('{')
        for i, (s, t) in enumerate(zip(self.stores, self.types)):
            if s == 'local':
                out.append('    %s l%d;' % (t, i))
            elif s == 'static':
                out.append('    static %s s%d;' % (t, i))
        for i, s in enumerate(self.stores):
            if s in ('local', 'static'):
                out.append('    %s%d = a%d;' % (s[0], i, i))
        e = self.text(self.tree)
        body = {'return': ['return (%s)(%s);' % (rt, e)],
                'if': ['if (%s) return 1;' % e, 'return 0;'],
                'arg': ['return take%d(%s);' % (n, e)],
                'global': ['o%d = %s;' % (n, e), 'return o%d;' % n]}[self.ctx]
        out.append('    {')
        for i, (s, t) in enumerate(zip(self.stores, self.types)):
            if s == 'block':
                out.append('        %s k%d = a%d;' % (t, i, i))
        out.extend('        ' + line for line in body)
        out.append('    }')
        out.append('}')
        out.append('')

    def emit_take(self, out):
        if self.ctx == 'arg':
            out.append('%s take%d(%s v) { return v; }' % (self.rt, self.n, self.rt))

    def emit_calls(self, out):
        n = self.n
        for k, values in enumerate(self.inputs):
            setup = []
            for i, (s, t, v) in enumerate(zip(self.stores, self.types, values)):
                if s == 'global':
                    setup.append('g%d_%d = %s;' % (n, i, literal(v, t)))
                elif s == 'array':
                    setup.append('r%d_%d[%d] = %s;' % (n, i, k % 3, literal(v, t)))
                elif s == 'member':
                    setup.append('m%d.f%d = %s;' % (n, i, literal(v, t)))
                elif s == 'ptr':
                    setup.append('v%d = %s;' % (i, literal(v, t)))
            decls = ''.join(' %s v%d;' % (t, i) for i, (s, t) in
                            enumerate(zip(self.stores, self.types)) if s == 'ptr')
            out.append('    {' + decls)
            out.extend('      ' + line for line in setup)
            args = [literal(v, t) for v, t in zip(values, self.types)] + [str(k % 3)] + \
                   ['&v%d' % i if s == 'ptr' else '0' for i, s in enumerate(self.stores)]
            out.append('      show(%d, (unsigned long)t%d(%s)); }' % (
                n * 10 + k, n, ', '.join(args)))

    def expected_for(self, k):
        return self.expected(self.inputs[k])


PROG_STORES = ['param', 'local', 'block', 'global', 'static', 'array', 'member']
PROG_STATEMENTS = ['assign'] * 3 + ['compound'] * 3 + ['incdec', 'ptr', 'bump', 'loop', 'if']


class ProgCase:
    """A straight-line program over several typed variables of mixed storage,
    some reached through pointers as well, whose statements assign, update,
    call and branch; the function returns a hash of every variable. Calls in
    expressions and a call that changes a global keep values live across
    calls and test that memory is reread after a store through an alias."""

    def __init__(self, n, rng):
        self.n = n
        count = rng.randint(4, 7)
        self.types = [rng.choice(NAMES) for _ in range(count)]
        self.stores = [rng.choice(PROG_STORES) for _ in range(count)]
        self.ptrs = rng.sample(range(count), rng.randint(0, 2))
        globals_ = [k for k, s in enumerate(self.stores) if s in ('global', 'member')]
        self.bumped = rng.choice(globals_) if globals_ else None
        self.body = [self.statement(rng, 1) for _ in range(rng.randint(4, 9))]
        self.inputs = []
        for _ in range(200):
            if len(self.inputs) == INPUTS:
                break
            values = tuple(interesting(t, rng) if rng.random() < 0.5
                           else wrap(rng.randint(-40, 40), t) for t in self.types)
            try:
                self.expected(values)
            except Undefined:
                continue
            self.inputs.append(values)

    def target(self, rng):
        """An lvalue: a variable or a pointer's target."""
        if self.ptrs and rng.random() < 0.25:
            return ('deref', rng.randrange(len(self.ptrs)))
        return ('var', rng.randrange(len(self.types)))

    def statement(self, rng, depth):
        kind = rng.choice(PROG_STATEMENTS if depth else PROG_STATEMENTS[:7])
        return self.make_statement(kind, rng, depth)

    def make_statement(self, kind, rng, depth):
        if kind == 'ptr':
            kind = 'assign' if not self.ptrs else rng.choice(['assign', 'compound'])
            lv = ('deref', rng.randrange(len(self.ptrs))) if self.ptrs else self.target(rng)
        else:
            lv = self.target(rng)
        if kind == 'assign':
            return ('assign', lv, self.expr(rng, rng.randint(0, 2)))
        if kind == 'compound':
            op = rng.choice(COMPOUND_OPS)
            rhs = ('const', rng.randint(0, 9), 'int') if op in ('<<', '>>') \
                else self.expr(rng, rng.randint(0, 1))
            return ('compound', op, lv, rhs)
        if kind == 'incdec':
            return ('incdec', rng.choice(['++x', '--x', 'x++', 'x--']), lv)
        if kind == 'bump':
            if self.bumped is None:
                return ('incdec', '++x', lv)
            return ('bump',)
        if kind == 'loop':
            return ('loop', rng.randint(1, 3),
                    [self.statement(rng, 0) for _ in range(rng.randint(1, 2))])
        return ('if', self.expr(rng, rng.randint(0, 2)), self.statement(rng, 0),
                self.statement(rng, 0))

    def expr(self, rng, depth):
        if depth == 0 or rng.random() < 0.2:
            r = rng.random()
            if r < 0.6:
                return self.target(rng)
            t = rng.choice(NAMES)
            return ('const', interesting(t, rng), t)
        kind = rng.choice(['binary'] * 5 + ['unary', 'cast', 'ternary', 'call'])
        if kind == 'binary':
            op = rng.choice(OPS)
            right = self.expr(rng, depth - 1)
            if op in ('<<', '>>'):
                right = ('cast', 'int', ('cast', 'unsigned char', right)) if rng.random() < 0.5 \
                    else ('const', rng.randint(0, 9), rng.choice(SHIFT_COUNT_TYPES))
            return ('binary', op, self.expr(rng, depth - 1), right)
        if kind == 'unary':
            return ('unary', rng.choice('-~!'), self.expr(rng, depth - 1))
        if kind == 'cast':
            return ('cast', rng.choice(NAMES), self.expr(rng, depth - 1))
        if kind == 'call':
            return ('call', self.expr(rng, depth - 1))
        return ('ternary', self.expr(rng, depth - 1), self.expr(rng, depth - 1),
                self.expr(rng, depth - 1))

    def var_of(self, lv):
        return lv[1] if lv[0] == 'var' else self.ptrs[lv[1]]

    def type_of(self, node):
        kind = node[0]
        if kind in ('var', 'deref'):
            return self.types[self.var_of(node)]
        if kind == 'const':
            return node[2]
        if kind == 'call':
            return 'long'
        if kind == 'binary':
            op, ta, tb = node[1], self.type_of(node[2]), self.type_of(node[3])
            if op in ('<<', '>>'):
                return promote(ta)
            if op in COMPARISONS + ('&&', '||'):
                return 'int'
            return common(ta, tb)
        if kind == 'unary':
            return 'int' if node[1] == '!' else promote(self.type_of(node[2]))
        if kind == 'cast':
            return node[1]
        return common(self.type_of(node[2]), self.type_of(node[3]))

    def evaluate(self, node, env):
        kind = node[0]
        if kind in ('var', 'deref'):
            k = self.var_of(node)
            return env[k], self.types[k]
        if kind == 'const':
            return wrap(node[1], node[2]), node[2]
        if kind == 'call':
            v, _ = self.evaluate(node[1], env)
            return wrap(v, 'long'), 'long'
        if kind == 'binary':
            op = node[1]
            a, ta = self.evaluate(node[2], env)
            if op in ('&&', '||') and (a != 0) == (op == '||'):
                return int(op == '||'), 'int'
            b, tb = self.evaluate(node[3], env)
            return binop(op, a, ta, b, tb)
        if kind == 'unary':
            v, t = self.evaluate(node[2], env)
            if node[1] == '!':
                return int(v == 0), 'int'
            pt = promote(t)
            v = wrap(v, pt)
            if node[1] == '~':
                return wrap(~v, pt), pt
            if signed(pt) and not lo(pt) <= -v <= hi(pt):
                raise Undefined
            return wrap(-v, pt), pt
        if kind == 'cast':
            v, _ = self.evaluate(node[2], env)
            return wrap(v, node[1]), node[1]
        c, _ = self.evaluate(node[1], env)
        rt = self.type_of(node)
        v, _ = self.evaluate(node[2] if c else node[3], env)
        return wrap(v, rt), rt

    def update(self, env, k, op, b, tb):
        t = self.types[k]
        v, _ = binop(op, env[k], t, b, tb)
        env[k] = wrap(v, t)

    def execute(self, stmt, env):
        kind = stmt[0]
        if kind == 'assign':
            v, _ = self.evaluate(stmt[2], env)
            env[self.var_of(stmt[1])] = wrap(v, self.types[self.var_of(stmt[1])])
        elif kind == 'compound':
            b, tb = self.evaluate(stmt[3], env)
            self.update(env, self.var_of(stmt[2]), stmt[1], b, tb)
        elif kind == 'incdec':
            k = self.var_of(stmt[2])
            pt = promote(self.types[k])
            v = wrap(env[k], pt) + (1 if '+' in stmt[1] else -1)
            if signed(pt) and not lo(pt) <= v <= hi(pt):
                raise Undefined
            env[k] = wrap(v, self.types[k])
        elif kind == 'bump':
            self.update(env, self.bumped, '+', 3, 'int')
        elif kind == 'loop':
            for _ in range(stmt[1]):
                for inner in stmt[2]:
                    self.execute(inner, env)
        else:
            c, _ = self.evaluate(stmt[1], env)
            self.execute(stmt[2] if c else stmt[3], env)

    def expected(self, values):
        env = [wrap(v, t) for v, t in zip(values, self.types)]
        for stmt in self.body:
            self.execute(stmt, env)
        h = 0
        for v in env:
            h = wrap(h * 31 + wrap(v, 'unsigned long'), 'unsigned long')
        return h

    def expected_for(self, k):
        return self.expected(self.inputs[k])

    def name(self, k):
        n = self.n
        return {'param': 'a%d' % k, 'local': 'l%d' % k, 'block': 'k%d' % k,
                'global': 'g%d_%d' % (n, k), 'static': 's%d' % k,
                'array': 'r%d_%d[i]' % (n, k), 'member': 'm%d.f%d' % (n, k)}[self.stores[k]]

    def text(self, node):
        kind = node[0]
        if kind == 'var':
            return self.name(node[1])
        if kind == 'deref':
            return '(*p%d)' % node[1]
        if kind == 'const':
            return literal(node[1], node[2])
        if kind == 'call':
            return 'pass%d(%s)' % (self.n, self.text(node[1]))
        if kind == 'binary':
            return '(%s %s %s)' % (self.text(node[2]), node[1], self.text(node[3]))
        if kind == 'unary':
            return '(%s(%s))' % (node[1], self.text(node[2]))
        if kind == 'cast':
            return '((%s)(%s))' % (node[1], self.text(node[2]))
        return '((%s) ? (%s) : (%s))' % (self.text(node[1]), self.text(node[2]),
                                         self.text(node[3]))

    def statement_text(self, stmt, indent, out):
        pad = ' ' * indent
        kind = stmt[0]
        if kind == 'assign':
            out.append('%s%s = %s;' % (pad, self.text(stmt[1]), self.text(stmt[2])))
        elif kind == 'compound':
            out.append('%s%s %s= %s;' % (pad, self.text(stmt[2]), stmt[1], self.text(stmt[3])))
        elif kind == 'incdec':
            out.append('%s%s;' % (pad, stmt[1].replace('x', self.text(stmt[2]))))
        elif kind == 'bump':
            out.append('%sbump%d();' % (pad, self.n))
        elif kind == 'loop':
            out.append('%sfor (j = 0; j < %d; ++j) {' % (pad, stmt[1]))
            for inner in stmt[2]:
                self.statement_text(inner, indent + 4, out)
            out.append('%s}' % pad)
        else:
            out.append('%sif (%s) {' % (pad, self.text(stmt[1])))
            self.statement_text(stmt[2], indent + 4, out)
            out.append('%s} else {' % pad)
            self.statement_text(stmt[3], indent + 4, out)
            out.append('%s}' % pad)

    def emit_take(self, out):
        n = self.n
        for k, (s, t) in enumerate(zip(self.stores, self.types)):
            if s == 'global':
                out.append('%s g%d_%d;' % (t, n, k))
            elif s == 'array':
                out.append('%s r%d_%d[3];' % (t, n, k))
        if 'member' in self.stores:
            out.append('struct { char pad; %s } m%d;' % (' '.join(
                '%s f%d;' % (t, k) for k, (s, t) in enumerate(zip(self.stores, self.types))
                if s == 'member'), n))
        out.append('static long pass%d(long v) { return v; }' % n)
        if self.bumped is not None:
            b = self.bumped
            out.append('static void bump%d(void) { %s += 3; }' % (
                n, 'g%d_%d' % (n, b) if self.stores[b] == 'global' else 'm%d.f%d' % (n, b)))

    def emit(self, out):
        n = self.n
        params = ['%s a%d' % (t, k) for k, t in enumerate(self.types)] + ['int i']
        out.append('unsigned long t%d(%s)' % (n, ', '.join(params)))
        out.append('{')
        for k, (s, t) in enumerate(zip(self.stores, self.types)):
            if s == 'local':
                out.append('    %s l%d;' % (t, k))
            elif s == 'static':
                out.append('    static %s s%d;' % (t, k))
        for k, s in enumerate(self.stores):
            if s not in ('param', 'block'):
                out.append('    %s = a%d;' % (self.name(k), k))
        out.append('    {')
        for k, (s, t) in enumerate(zip(self.stores, self.types)):
            if s == 'block':
                out.append('        %s k%d = a%d;' % (t, k, k))
        for j, k in enumerate(self.ptrs):
            out.append('        %s *p%d = &%s;' % (self.types[k], j, self.name(k)))
        out.append('        int j;')
        out.append('        unsigned long h;')
        for stmt in self.body:
            self.statement_text(stmt, 8, out)
        out.append('        h = 0;')
        for k in range(len(self.types)):
            out.append('        h = h * 31 + (unsigned long)%s;' % self.name(k))
        out.append('        return h;')
        out.append('    }')
        out.append('}')
        out.append('')

    def emit_calls(self, out):
        n = self.n
        for k, values in enumerate(self.inputs):
            args = [literal(v, t) for v, t in zip(values, self.types)] + [str(k % 3)]
            out.append('    show(%d, t%d(%s));' % (n * 10 + k, n, ', '.join(args)))


class ScopeCase(ProgCase):
    """A ProgCase whose statements also open blocks declaring a temporary `t`
    of a random type (shadowing an outer `t` when nested), switch on a value
    into cases that each declare their own `t` and may fall through, loop
    with do/while, skip statements with a forward goto, and read or write
    single bytes of a wider variable through an unsigned char pointer."""

    def __init__(self, n, rng):
        self.scope = []           # types of the temporaries in scope
        self.temps = []           # their values while the model runs
        self.labels = 0
        super().__init__(n, rng)

    def statement(self, rng, depth):
        kinds = PROG_STATEMENTS[:7] + ['bytes']
        if depth:
            kinds += PROG_STATEMENTS[7:] + ['block'] * 2 + ['switch', 'dowhile', 'skip']
        elif len(self.scope) < 2:
            kinds += ['block']
        kind = rng.choice(kinds)
        if kind == 'block':
            return self.block(rng)
        if kind == 'switch':
            selector = ('cast', 'int', ('binary', '&', self.expr(rng, rng.randint(0, 2)),
                                        ('const', 3, 'int')))
            return ('switch', selector,
                    [(self.block(rng), rng.random() < 0.7) for _ in range(4)])
        if kind == 'dowhile':
            return ('dowhile', rng.randint(1, 3),
                    [self.statement(rng, 0) for _ in range(rng.randint(1, 2))])
        if kind == 'skip':
            self.labels += 1
            return ('skip', self.labels, self.expr(rng, rng.randint(0, 2)),
                    [self.statement(rng, 0) for _ in range(rng.randint(1, 2))])
        if kind == 'bytes':
            k = rng.randrange(len(self.types))
            return ('bytes', k, rng.randrange(bits(self.types[k]) // 8),
                    self.expr(rng, rng.randint(0, 2)))
        return self.make_statement(kind, rng, depth)

    def block(self, rng):
        """{ T t = init; body } with the new t visible only in the body."""
        t = rng.choice(NAMES)
        hidden, self.scope = self.scope, []
        init = self.expr(rng, rng.randint(0, 2))    # C scopes t from its declarator
        self.scope = hidden + [t]
        body = [self.statement(rng, 0) for _ in range(rng.randint(1, 3))]
        if rng.random() < 0.7:
            body.append(('assign', ('var', rng.randrange(len(self.types))),
                         ('binary', rng.choice(['+', '-', '^', '|']), ('temp', t),
                          self.expr(rng, rng.randint(0, 1)))))
        self.scope = hidden
        return ('block', t, init, body)

    def target(self, rng):
        if self.scope and rng.random() < 0.3:
            return ('temp', self.scope[-1])
        return super().target(rng)

    def expr(self, rng, depth):
        if (depth == 0 or rng.random() < 0.2) and rng.random() < 0.1:
            k = rng.randrange(len(self.types))
            return ('byte', k, rng.randrange(bits(self.types[k]) // 8))
        return super().expr(rng, depth)

    def type_of(self, node):
        if node[0] == 'temp':
            return node[1]
        if node[0] == 'byte':
            return 'unsigned char'
        return super().type_of(node)

    def evaluate(self, node, env):
        if node[0] == 'temp':
            return self.temps[-1], node[1]
        if node[0] == 'byte':
            k = node[1]
            return (wrap(env[k], 'unsigned long') >> (8 * node[2])) & 255, 'unsigned char'
        return super().evaluate(node, env)

    def store(self, env, lv, value):
        if lv[0] == 'temp':
            self.temps[-1] = wrap(value, lv[1])
        else:
            k = self.var_of(lv)
            env[k] = wrap(value, self.types[k])

    def load(self, env, lv):
        if lv[0] == 'temp':
            return self.temps[-1], lv[1]
        k = self.var_of(lv)
        return env[k], self.types[k]

    def execute(self, stmt, env):
        kind = stmt[0]
        if kind == 'assign':
            self.store(env, stmt[1], self.evaluate(stmt[2], env)[0])
        elif kind == 'compound':
            b, tb = self.evaluate(stmt[3], env)
            a, ta = self.load(env, stmt[2])
            self.store(env, stmt[2], binop(stmt[1], a, ta, b, tb)[0])
        elif kind == 'incdec':
            a, ta = self.load(env, stmt[2])
            pt = promote(ta)
            v = wrap(a, pt) + (1 if '+' in stmt[1] else -1)
            if signed(pt) and not lo(pt) <= v <= hi(pt):
                raise Undefined
            self.store(env, stmt[2], v)
        elif kind == 'block':
            self.temps.append(wrap(self.evaluate(stmt[2], env)[0], stmt[1]))
            for inner in stmt[3]:
                self.execute(inner, env)
            self.temps.pop()
        elif kind == 'switch':
            first = self.evaluate(stmt[1], env)[0]
            for body, ends_with_break in stmt[2][first:]:
                self.execute(body, env)
                if ends_with_break:
                    break
        elif kind == 'dowhile':
            for _ in range(stmt[1]):
                for inner in stmt[2]:
                    self.execute(inner, env)
        elif kind == 'skip':
            if not self.evaluate(stmt[2], env)[0]:
                for inner in stmt[3]:
                    self.execute(inner, env)
        elif kind == 'bytes':
            k, shift = stmt[1], 8 * stmt[2]
            v = wrap(env[k], 'unsigned long') & ~(255 << shift)
            v |= wrap(self.evaluate(stmt[3], env)[0], 'unsigned char') << shift
            env[k] = wrap(v, self.types[k])
        else:
            super().execute(stmt, env)

    def expected(self, values):
        self.temps = []
        return super().expected(values)

    def text(self, node):
        if node[0] == 'temp':
            return 't'
        if node[0] == 'byte':
            return '((unsigned char *)&%s)[%d]' % (self.name(node[1]), node[2])
        return super().text(node)

    def statement_text(self, stmt, indent, out):
        pad = ' ' * indent
        kind = stmt[0]
        if kind == 'block':
            out.append('%s{' % pad)
            out.append('%s    %s t = %s;' % (pad, stmt[1], self.text(stmt[2])))
            for inner in stmt[3]:
                self.statement_text(inner, indent + 4, out)
            out.append('%s}' % pad)
        elif kind == 'switch':
            out.append('%sswitch (%s) {' % (pad, self.text(stmt[1])))
            for label, (body, ends_with_break) in enumerate(stmt[2]):
                out.append('%s%s:' % (pad, 'default' if label == 3 else 'case %d' % label))
                self.statement_text(body, indent + 4, out)
                if ends_with_break:
                    out.append('%s    break;' % pad)
            out.append('%s}' % pad)
        elif kind == 'dowhile':
            out.append('%sj = %d;' % (pad, stmt[1]))
            out.append('%sdo {' % pad)
            for inner in stmt[2]:
                self.statement_text(inner, indent + 4, out)
            out.append('%s} while (--j > 0);' % pad)
        elif kind == 'skip':
            out.append('%sif (%s)' % (pad, self.text(stmt[2])))
            out.append('%s    goto skip%d;' % (pad, stmt[1]))
            for inner in stmt[3]:
                self.statement_text(inner, indent, out)
            out.append('%sskip%d: ;' % (pad, stmt[1]))
        elif kind == 'bytes':
            out.append('%s((unsigned char *)&%s)[%d] = (unsigned char)(%s);' % (
                pad, self.name(stmt[1]), stmt[2], self.text(stmt[3])))
        else:
            super().statement_text(stmt, indent, out)


HEADER = '''/* generated by typed-expr-fuzz.py, seed %d */
#include <stdio.h>

static void hex(unsigned long v)
{
    int k;
    for (k = 28; k >= 0; k -= 4)
        putchar("0123456789abcdef"[(int)((v >> k) & 15)]);
}

static void show(int id, unsigned long v)
{
    hex((unsigned long)id);
    putchar(' ');
    hex(v);
    putchar('\\n');
}
'''


def generate(seed, count, mode='binary'):
    """(C source, expected output) for one seed."""
    rng = random.Random(seed)
    cases = []
    while len(cases) < count:
        if mode == 'store':
            kind = StoreCase
        elif mode == 'prog':
            kind = ProgCase
        elif mode == 'scope':
            kind = ScopeCase
        else:
            kind = Case if mode == 'binary' or (mode == 'mixed' and rng.random() < 0.5) \
                else TreeCase
        case = kind(len(cases), rng)
        if case.inputs:
            cases.append(case)
    out = [HEADER % seed]
    for case in cases:
        case.emit_take(out)
    for case in cases:
        case.emit(out)
    out += ['int main(void)', '{']
    for case in cases:
        case.emit_calls(out)
    out += ['    return 0;', '}']
    expected = ''.join('%08x %08x\n' % ((case.n * 10 + k) & 0xffffffff,
                                        wrap(case.expected_for(k), 'unsigned long'))
                       for case in cases for k in range(len(case.inputs)))
    return '\n'.join(out) + '\n', expected


def run_seed(seed, args):
    """'ok', or a one-line description of the first failure."""
    work = args.work / str(seed)
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    source, expected = generate(seed, args.count, args.mode)
    (work / 't.c').write_text(source)
    (work / 't.expected').write_text(expected)
    env = dict(os.environ)
    for name in args.env:
        key, _, value = name.partition('=')
        env[key] = value
    failure = None
    for peep in ('True', 'False'):
        build = work / ('peep' if peep == 'True' else 'nopeep')
        try:
            result = subprocess.run(
                [str(REPO / 'dccmake'), 'dcc-input=%s' % (work / 't.c'), 'dcc-output=T',
                 'dcc-build-dir=%s' % build, 'dcc-peep=%s' % peep,
                 'ntvcm-tool=%s' % args.emulator],
                cwd=REPO, env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                timeout=args.build_timeout)
        except subprocess.TimeoutExpired:
            failure = 'HANG building %s (over %d s)' % (build.name, args.build_timeout)
            break
        log = result.stdout + result.stderr
        (work / ('build-%s.log' % build.name)).write_text(log)
        if result.returncode != 0:
            if 'program too large' in log or 'segment too large' in log:
                # Not a compiler fault: the program outgrew the 64K address space.
                failure = 'SKIP %s too large to link' % build.name
                break
            lines = [line for line in log.splitlines() if 'error DCC' in line]
            failure = 'COMPILE-FAIL ' + (lines[0].split('error ', 1)[-1] if lines else
                                         'build failed (%s)' % build.name)
            break
        try:
            run = subprocess.run([args.emulator, 'T.COM'], cwd=build, stdin=subprocess.DEVNULL,
                                 capture_output=True, text=True, timeout=args.timeout)
        except subprocess.TimeoutExpired:
            failure = 'HANG running %s (over %d s)' % (build.name, args.timeout)
            break
        output = run.stdout.replace('\r', '')
        (work / ('out-%s' % build.name)).write_text(output)
        if 'too large to fit in memory' in output or 'TPA too small' in output:
            # Not a compiler fault: the unoptimized program outgrew the TPA.
            failure = 'SKIP %s too large to run' % build.name
            break
        if output != expected:
            wrong = sum(1 for want, got in zip(expected.splitlines(), output.splitlines())
                        if want != got)
            failure = 'MISMATCH %s, %d values' % (build.name, wrong or 1)
            break
    if (failure is None or failure.startswith('SKIP')) and not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    return failure or 'ok'


def parse_seeds(text):
    seeds = []
    for part in text.split(','):
        low, _, high = part.partition('-')
        seeds.extend(range(int(low), int(high or low) + 1))
    return seeds


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--seeds', default='1-100', help='seeds to run, e.g. 1-100,250')
    parser.add_argument('--count', type=int,
                        help='cases per program (40, or 12 for scope, whose cases are larger)')
    parser.add_argument('--mode', choices=('binary', 'tree', 'mixed', 'store', 'prog', 'scope'), default='binary',
                        help='one binary operation per case, nested expression trees, both, '
                             'or updates of lvalues (compound assignment, ++, --)')
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    parser.add_argument('--work', type=Path, default=REPO / 'build' / 'typed-expr-fuzz')
    parser.add_argument('--emulator', default='ntvcm')
    parser.add_argument('--timeout', type=int, default=60, help='seconds to run a program')
    parser.add_argument('--build-timeout', type=int, default=300,
                        help='seconds to build a program; longer is reported as a hang')
    parser.add_argument('--keep', action='store_true', help='keep passing seeds too')
    parser.add_argument('--env', action='append', default=[],
                        help='NAME=VALUE for dcc, e.g. DCC_MIR_SELECT_CANDIDATE=homed')
    parser.add_argument('--generate', nargs=3, metavar=('SEED', 'C', 'EXPECTED'),
                        help='only write one program and its expected output')
    args = parser.parse_args()
    if args.count is None:
        args.count = 12 if args.mode == 'scope' else 40
    if args.generate:
        source, expected = generate(int(args.generate[0]), args.count, args.mode)
        Path(args.generate[1]).write_text(source)
        Path(args.generate[2]).write_text(expected)
        return 0
    seeds = parse_seeds(args.seeds)
    failures = 0
    skipped = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for seed, outcome in zip(seeds, pool.map(lambda s: run_seed(s, args), seeds)):
            if outcome.startswith('SKIP'):
                skipped += 1
                print('seed %d: %s' % (seed, outcome))
            elif outcome != 'ok':
                failures += 1
                print('seed %d: %s (%s)' % (seed, outcome, args.work / str(seed)))
    print('%d of %d programs passed%s' % (
        len(seeds) - failures - skipped, len(seeds) - skipped,
        ' (%d too large, skipped)' % skipped if skipped else ''))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
