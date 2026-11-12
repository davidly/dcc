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
            lines = [line for line in log.splitlines() if 'error DCC' in line]
            failure = 'COMPILE-FAIL ' + (lines[0].split('error ', 1)[-1] if lines else
                                         'build failed (%s)' % build.name)
            break
        run = subprocess.run([args.emulator, 'T.COM'], cwd=build, stdin=subprocess.DEVNULL,
                             capture_output=True, text=True, timeout=args.timeout)
        output = run.stdout.replace('\r', '')
        (work / ('out-%s' % build.name)).write_text(output)
        if output != expected:
            wrong = sum(1 for want, got in zip(expected.splitlines(), output.splitlines())
                        if want != got)
            failure = 'MISMATCH %s, %d values' % (build.name, wrong or 1)
            break
    if failure is None and not args.keep:
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
    parser.add_argument('--count', type=int, default=40, help='cases per program')
    parser.add_argument('--mode', choices=('binary', 'tree', 'mixed'), default='binary',
                        help='one binary operation per case, nested expression trees, or both')
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
    if args.generate:
        source, expected = generate(int(args.generate[0]), args.count, args.mode)
        Path(args.generate[1]).write_text(source)
        Path(args.generate[2]).write_text(expected)
        return 0
    seeds = parse_seeds(args.seeds)
    failures = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for seed, outcome in zip(seeds, pool.map(lambda s: run_seed(s, args), seeds)):
            if outcome != 'ok':
                failures += 1
                print('seed %d: %s (%s)' % (seed, outcome, args.work / str(seed)))
    print('%d of %d programs passed' % (len(seeds) - failures, len(seeds)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
