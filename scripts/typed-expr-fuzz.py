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


def generate(seed, count):
    """(C source, expected output) for one seed."""
    rng = random.Random(seed)
    cases = []
    while len(cases) < count:
        case = Case(len(cases), rng)
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
                                        wrap(case.expected(a, b), 'unsigned long'))
                       for case in cases for k, (a, b) in enumerate(case.inputs))
    return '\n'.join(out) + '\n', expected


def run_seed(seed, args):
    """'ok', or a one-line description of the first failure."""
    work = args.work / str(seed)
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    source, expected = generate(seed, args.count)
    (work / 't.c').write_text(source)
    (work / 't.expected').write_text(expected)
    env = dict(os.environ)
    for name in args.env:
        key, _, value = name.partition('=')
        env[key] = value
    failure = None
    for peep in ('True', 'False'):
        build = work / ('peep' if peep == 'True' else 'nopeep')
        result = subprocess.run(
            [str(REPO / 'dccmake'), 'dcc-input=%s' % (work / 't.c'), 'dcc-output=T',
             'dcc-build-dir=%s' % build, 'dcc-peep=%s' % peep, 'ntvcm-tool=%s' % args.emulator],
            cwd=REPO, env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True)
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
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    parser.add_argument('--work', type=Path, default=REPO / 'build' / 'typed-expr-fuzz')
    parser.add_argument('--emulator', default='ntvcm')
    parser.add_argument('--timeout', type=int, default=60)
    parser.add_argument('--keep', action='store_true', help='keep passing seeds too')
    parser.add_argument('--env', action='append', default=[],
                        help='NAME=VALUE for dcc, e.g. DCC_MIR_SELECT_CANDIDATE=homed')
    parser.add_argument('--generate', nargs=3, metavar=('SEED', 'C', 'EXPECTED'),
                        help='only write one program and its expected output')
    args = parser.parse_args()
    if args.generate:
        source, expected = generate(int(args.generate[0]), args.count)
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
