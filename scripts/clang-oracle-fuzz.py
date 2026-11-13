#!/usr/bin/env python3
"""Differential fuzzer for dcc whose expected output comes from the host C
compiler.

Each seed generates one C program (scripts/clang_oracle_gen.py): helper and
test functions over fixed-width integers, arrays, structs, bit-fields and
pointers, with loops, switches, nested-block locals, gotos, calls, recursion
and calls through function pointers. The same source is compiled by the host
compiler and by dcc, and the two outputs are compared.

A program is used only when a host build with the checking layer, ASan and
UBSan reports nothing that is undefined or that would differ with dcc's
16-bit int; tripped operations are repaired and the program is checked again.
So a mismatch is a dcc defect, not a porting difference.

  clang-oracle-fuzz.py --seeds 1-200                 fuzz, production configs
  clang-oracle-fuzz.py --seeds 1-200 --configs all   also force every MIR candidate
  clang-oracle-fuzz.py --reduce 17 --config nopeep   shrink seed 17's failure
  clang-oracle-fuzz.py --generate 17 prog.c          only write seed 17's program
  clang-oracle-fuzz.py --list-configs

A failing seed keeps build/clang-oracle-fuzz/<seed>/ with t.c, t.expected and
each failing config's output. An output line is a call number and the returned
value, or 0x4000 + the call number and a checksum of every global after that
call. A seed names a program only together with --tests, --size, --depth and
--off; a reduced program is self-contained.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import copy
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import clang_oracle_gen as gen  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
HOST_FLAGS = ['-std=gnu99', '-w', '-fsigned-char', '-fno-common']
SANITIZE = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
CANDIDATES = ['homed', 'homed-lazy', 'hybrid', 'regional', 'spilled-baseline', 'spilled-all',
              'spilled-all-no-prepack', 'spilled-address-remat', 'spilled-boolean-phi-branch',
              'spilled-boolean-phi-branch-no-prepack', 'spilled-global-argument',
              'spilled-phi-slot', 'spilled-promoted-local-slot', 'spilled-rhs-forward',
              'spilled-stable-local', 'spilled-stable-pointer-argument',
              'spilled-stack-argument', 'spilled-store-address', 'spilled-wide-binary-lhs']
DIAGNOSTIC_ENV = ('DCC_MIR_NO_EXACT', 'DCC_MIR_SELECT_CANDIDATE', 'DCC_MIR_PREFER_REGIONAL',
                  'DCC_MIR_SELECT_FUNCTION', 'DCC_FORCE_STACK_CHECK')


class Config:
    def __init__(self, name, env, peep, stack_check, production):
        self.name, self.env, self.peep, self.stack_check = name, env, peep, stack_check
        self.production = production


def all_configs():
    out = []
    for peep in (False, True):
        tag = 'peep' if peep else 'nopeep'
        out.append(Config(tag, {}, peep, True, True))
        out.append(Config(tag + '-nosc', {}, peep, False, True))
    for peep in (False, True):
        tag = 'peep' if peep else 'nopeep'
        out.append(Config('noexact-' + tag, {'DCC_MIR_NO_EXACT': '1'}, peep, True, False))
        for cand in CANDIDATES:
            env = {'DCC_MIR_NO_EXACT': '1'}
            if cand == 'regional':
                env['DCC_MIR_PREFER_REGIONAL'] = '1'
            else:
                env['DCC_MIR_SELECT_CANDIDATE'] = cand
            out.append(Config('%s-%s' % (cand, tag), env, peep, True, False))
    return out


CONFIGS = {c.name: c for c in all_configs()}


def select_configs(text):
    if text == 'production':
        return [c for c in CONFIGS.values() if c.production]
    if text == 'default':
        return [c for c in CONFIGS.values() if c.production or c.name.startswith('noexact-')]
    if text == 'all':
        return list(CONFIGS.values())
    return [CONFIGS[name] for name in text.split(',')]


# ------------------------------------------------------------- host oracle

def host_build(source, exe, flags, timeout=120):
    r = subprocess.run(['clang', *HOST_FLAGS, *flags, str(source), '-o', str(exe)],
                       capture_output=True, text=True, timeout=timeout)
    return r.returncode == 0, r.stderr


def host_exec(exe, timeout):
    try:
        r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=timeout,
                           stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        return None, '', 'timeout'
    return r.returncode, r.stdout, r.stderr


def checked_run(prog, work, timeout, sanitize=True):
    """(status, detail, output) of the checked build: ok, trips, or invalid."""
    src = work / 'check.c'
    src.write_text(gen.render(prog, checked=True))
    flags = ['-O1', *SANITIZE] if sanitize else ['-O0']
    ok, err = host_build(src, work / 'check.host', flags)
    if not ok:
        return 'invalid', 'checked build does not compile: ' + err[:400], ''
    code, out, err = host_exec(work / 'check.host', timeout)
    if code is None:
        return 'invalid', 'checked build timed out', ''
    if code != 0:
        return 'invalid', 'checked build failed: ' + err.strip()[-400:], ''
    trips = [int(x) for x in re.findall(r'TRIP (\d+)', err)]
    if trips:
        return 'trips', trips, out
    return 'ok', None, out


def host_oracle(prog, work, repair=True, strict=True, timeout=10, rounds=40):
    """Repair prog until it is defined, then return (expected output, None),
    or (None, reason) when it cannot be used.

    strict also requires -O0 and a sanitized build of the natural source to
    agree with the -O2 build, and sanitizes the checked build; the checked
    build must always agree. The reducer validates candidates without strict
    and its result with it."""
    work.mkdir(parents=True, exist_ok=True)
    for _ in range(rounds):
        status, detail, checked_out = checked_run(prog, work, timeout, sanitize=strict)
        if status == 'ok':
            break
        if status == 'invalid':
            return None, detail
        if not repair:
            return None, 'trips'
        left = gen.repair(prog, detail)
        if left:
            return None, 'unrepairable trips %s' % sorted(left)
    else:
        return None, 'still tripping after %d repairs' % rounds
    src = work / 't.c'
    src.write_text(gen.render(prog, title=', seed %s' % prog.seed))
    # clang proves at compile time what no run can: an object modified twice,
    # or modified and read, with no sequence point between; and a value
    # returning function that can fall off its end
    lint = subprocess.run(['clang', '-std=gnu99', '-fsigned-char', '-fsyntax-only',
                           '-Wno-everything', '-Werror=unsequenced', '-Werror=return-type',
                           str(src)], capture_output=True, text=True)
    if lint.returncode != 0:
        return None, 'not well defined: ' + lint.stderr.strip().splitlines()[0][-160:]
    builds = [('O2', ['-O2'])]
    if strict:
        builds += [('O0', ['-O0']), ('san', ['-O1', *SANITIZE])]
    outputs = []
    for tag, flags in builds:
        ok, err = host_build(src, work / ('t.%s.host' % tag), flags)
        if not ok:
            return None, 'natural build (%s) does not compile: %s' % (tag, err[:400])
        code, out, err = host_exec(work / ('t.%s.host' % tag), timeout)
        if code != 0:
            return None, 'natural build (%s) failed: %s' % (tag, (err or 'timeout')[-300:])
        outputs.append(out)
    if any(o != outputs[0] for o in outputs) or checked_out != outputs[0]:
        return None, 'host builds disagree'
    (work / 't.expected').write_text(outputs[0])
    return outputs[0], None


# ----------------------------------------------------------------- dcc side

def dcc_run(source, build, cfg, args):
    """(kind, detail, output) of building and running source with dcc.
    kind: ok, COMPILE, BUILD, BUILD-HANG, VERIFY or RUN-HANG."""
    shutil.rmtree(build, ignore_errors=True)
    env = dict(os.environ)
    for key in DIAGNOSTIC_ENV:
        env.pop(key, None)
    env.update(cfg.env)
    cmd = [str(REPO / 'dccmake'), 'dcc-input=%s' % source, 'dcc-output=T',
           'dcc-build-dir=%s' % build, 'dcc-peep=%s' % ('true' if cfg.peep else 'false'),
           'dcc-stack-check=%s' % ('true' if cfg.stack_check else 'false'),
           'dcc-stack-bytes=%d' % args.stack, 'dcc-floatio=true', 'dcc-flongio=true',
           'ntvcm-tool=%s' % args.emulator]
    try:
        r = subprocess.run(cmd, cwd=REPO, env=env, stdin=subprocess.DEVNULL,
                           capture_output=True, text=True, timeout=args.build_timeout)
    except subprocess.TimeoutExpired:
        return 'BUILD-HANG', 'build ran past %d s' % args.build_timeout, ''
    log = r.stdout + r.stderr
    if r.returncode != 0:
        lines = [ln for ln in log.splitlines() if not ln.startswith('+')]
        errors = [ln for ln in lines if 'error' in ln.lower() or 'fatal' in ln.lower()
                  or 'MIR' in ln]
        first = (errors or lines or ['build failed'])[0].strip()
        first = first.replace(str(source) + ':', '')
        if 'error DCC' in first or 'dcc: fatal' in first or 'MIR' in first:
            return 'COMPILE', first[:200], log
        return 'BUILD', first[:200], log
    verify = re.search(r'^; MIR \S+: (instruction \d+ .*)$', log, re.M)
    if verify:
        # dcc reported invalid MIR and still produced a program
        return 'VERIFY', re.sub(r'\d+', 'N', verify.group(1))[:120], log
    try:
        run = subprocess.run([args.emulator, '-s:0', 'T.COM'], cwd=build,
                             stdin=subprocess.DEVNULL, capture_output=True, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        return 'RUN-HANG', 'ran past %d s' % args.timeout, ''
    return 'ok', '', run.stdout.decode('latin-1').replace('\r', '')


def judge(kind, detail, output, expected):
    if kind != 'ok':
        return kind, detail
    if output == expected:
        return 'ok', ''
    if 'stack overflow' in output:
        return 'STACK', 'stack overflow'
    want, got = expected.splitlines(), output.splitlines()
    wrong = [w.split()[0] for w, g in zip(want, got) if w != g]
    if len(got) != len(want):
        return 'MISMATCH', '%d of %d lines printed, first wrong %s' % (
            len(got), len(want), wrong[0] if wrong else 'none')
    return 'MISMATCH', 'lines %s' % ' '.join(wrong[:6])


def signature(kind, detail):
    """What a reduction must preserve."""
    if kind in ('COMPILE', 'BUILD', 'VERIFY'):
        m = re.search(r'(DCC-[A-Z]\d+: .*?)(?: near |$)', detail)
        if m:
            return kind, m.group(1)
        return kind, re.sub(r'\d+', 'N', re.sub(r'function \w+', 'function F', detail))[-70:]
    return kind, ''


# --------------------------------------------------------------------- fuzz

def make_program(seed, args):
    return gen.generate(seed, tests=args.tests, size=args.size, depth=args.depth,
                        off=args.off)


def run_seed(seed, args, configs):
    work = args.work / str(seed)
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    try:
        prog = make_program(seed, args)
        expected, reason = host_oracle(prog, work)
    except Exception as exc:  # a generator defect must not stop the campaign
        return seed, 'GENERATOR', {'-': ('GENERATOR', repr(exc)[:200])}
    if expected is None:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
        return seed, 'INVALID', {'-': ('INVALID', reason)}
    failures = {}
    for cfg in configs:
        build = work / cfg.name
        kind, detail, output = dcc_run(work / 't.c', build, cfg, args)
        kind, detail = judge(kind, detail, output, expected)
        shutil.rmtree(build, ignore_errors=True)
        if kind != 'ok':
            failures[cfg.name] = (kind, detail)
            if output:
                (work / ('out-%s.txt' % cfg.name)).write_text(output)
    if not failures and not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    return seed, 'FAIL' if failures else 'ok', failures


def summarize(failures):
    """Group a seed's failing configs by (kind, detail)."""
    groups = {}
    for name, (kind, detail) in failures.items():
        groups.setdefault((kind, detail), []).append(name)
    parts = []
    for (kind, detail), names in sorted(groups.items()):
        shown = ','.join(names[:4]) + (',+%d' % (len(names) - 4) if len(names) > 4 else '')
        parts.append('%s [%s] %s' % (kind, shown, detail))
    return '; '.join(parts)


def fuzz(args):
    configs = select_configs(args.configs)
    seeds = parse_seeds(args.seeds)
    counts = {}
    failing = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for seed, status, failures in pool.map(lambda s: run_seed(s, args, configs), seeds):
            if status in ('INVALID', 'GENERATOR'):
                counts[status] = counts.get(status, 0) + 1
                if args.verbose or status == 'GENERATOR':
                    print('seed %d: %s %s' % (seed, status, failures['-'][1]))
                continue
            real = {k: v for k, v in failures.items() if v[0] != 'STACK'}
            if not real:
                status = 'stack-only' if failures else 'ok'
                counts[status] = counts.get(status, 0) + 1
                continue
            counts['FAIL'] = counts.get('FAIL', 0) + 1
            failing.append(seed)
            print('seed %d: %s' % (seed, summarize(real)))
            sys.stdout.flush()
    print('%d programs: %s' % (len(seeds), ', '.join('%s %d' % kv for kv in sorted(counts.items()))))
    return 1 if failing else 0


# ------------------------------------------------------------------- reduce

def returns_value(prog, block):
    """block is the body of a function that returns a value."""
    return any(f.body is block and f.ret is not None for f in prog.funcs)


def referenced(prog):
    used = set()
    for slot in gen.expr_slots(prog):
        e = gen.slot_get(slot)
        if e.op in ('var', 'idx', 'pidx', 'aend', 'addrof'):
            used.add(e.a)
        elif e.op == 'addr':
            used.add(e.a[0])
    for b in gen.all_blocks(prog):
        for s in b.stmts:
            if s.op == 'for' and s.a is not None:
                used.add(s.a)
    for f in prog.funcs:
        for args in f.inputs:
            for a in args:
                if a.op in ('var', 'idx', 'addrof'):
                    used.add(a.a)
                elif a.op == 'addr':
                    used.add(a.a[0])
    return used


def parented_slots(prog):
    """[(slot, parent expression or None, child index)] in a stable order.
    Loop headers are left out: simplifying one usually makes the loop endless."""
    roots = []

    def block(b):
        for d in b.decls:
            if isinstance(d.init, gen.E):
                roots.append((d, 'init'))
        for s in b.stmts:
            if s.op not in ('for', 'while', 'do'):
                roots.extend((s.e, i) for i, e in enumerate(s.e) if e is not None)
            for sub in s.b:
                if sub is not None:
                    block(sub)

    for f in prog.funcs:
        block(f.body)
    out = []
    stack = [(slot, None, 0) for slot in reversed(roots)]
    while stack:
        slot, parent, index = stack.pop()
        out.append((slot, parent, index))
        e = gen.slot_get(slot)
        for i in range(len(e.k) - 1, -1, -1):
            stack.append(((e.k, i), e, i))
    return out


def is_lvalue_slot(parent, index):
    if parent is None:
        return False
    if parent.op in ('asg', 'incdec') and index == 0:
        return True
    return parent.op in ('fld', 'deref') and index == 0


class Reducer:
    """Shrinks a failing program. Every pass is a list of candidate edits in
    program order; candidates are tried in parallel and the first that keeps
    the failure is applied."""

    def __init__(self, prog, cfg, want, args, work):
        self.prog, self.cfg, self.want, self.args, self.work = prog, cfg, want, args, work
        self.tests = 0
        self.pool = concurrent.futures.ThreadPoolExecutor(max_workers=args.reduce_jobs)
        # an edit may not turn this failure into one that is already logged
        self.shapes = gen.known_shapes(prog)

    def still_fails(self, cand, lane=0):
        """cand is defined on the host and dcc still fails the same way."""
        self.tests += 1
        work = self.work / ('try%d' % lane)
        work.mkdir(parents=True, exist_ok=True)
        src = work / 't.c'
        try:
            src.write_text(gen.render(cand))
        except Exception:
            return False
        expected = None
        if self.want[0] not in ('COMPILE', 'BUILD', 'BUILD-HANG', 'VERIFY'):
            ok, _ = host_build(src, work / 't.host', ['-O1'], timeout=60)
            if not ok:
                return False
            code, expected, _ = host_exec(work / 't.host', 5)
            if code != 0:
                return False
        kind, detail, output = dcc_run(src, work / 'dcc', self.cfg, self.args)
        if kind == 'ok' and expected is None:
            return False
        kind, detail = judge(kind, detail, output, expected)
        if signature(kind, detail) != self.want:
            return False
        checked, _ = host_oracle(copy.deepcopy(cand), work / 'host', repair=False,
                                 strict=False, timeout=5)
        return checked is not None and (expected is None or checked == expected)

    def sweep(self, candidates):
        """candidates(prog) lists edits in program order; each returns False to
        decline. Edits that survive alone are applied together, last first so
        earlier positions stay valid, when the failure survives that too."""
        changed = False
        pos = 0
        while True:
            cands = candidates(self.prog)
            if pos >= len(cands):
                return changed
            batch = cands[pos:pos + self.args.reduce_jobs]

            def apply(edits):
                cand = copy.deepcopy(self.prog)
                try:
                    for mutate in edits:
                        if mutate(cand) is False:
                            return None
                    if gen.known_shapes(cand) - self.shapes:
                        return None
                except (IndexError, AttributeError, ValueError, KeyError, TypeError,
                        StopIteration):
                    return None
                return cand

            def evaluate(item):
                lane, mutate = item
                cand = apply([mutate])
                return cand if cand is not None and self.still_fails(cand, lane) else None

            results = list(self.pool.map(evaluate, enumerate(batch)))
            hits = [k for k, c in enumerate(results) if c is not None]
            if not hits:
                pos += len(batch)
                continue
            changed = True
            # apply as many of the surviving edits together as still fail
            n = len(hits)
            while n > 1:
                together = apply([batch[k] for k in reversed(hits[:n])])
                if together is not None and self.still_fails(together):
                    self.prog = together
                    break
                n //= 2
            else:
                self.prog = results[hits[0]]
            pos += hits[0]

    # ---- candidate lists

    @staticmethod
    def only_tests(prog):
        names = [f.name for f in prog.tests]
        if len(names) < 2:
            return []

        def only(keep):
            def mutate(p):
                p.funcs = [f for f in p.funcs if f.kind != 'test' or f.name == keep]
            return mutate
        return [only(n) for n in names]

    @staticmethod
    def fewer_tests(prog):
        out = []
        tests = prog.tests
        if len(tests) > 1:
            for f in tests:
                def drop(p, name=f.name):
                    p.funcs = [g for g in p.funcs if g.name != name]
                out.append(drop)
        for f in tests:
            if len(f.inputs) > 1:
                for i in range(len(f.inputs)):
                    def drop_input(p, name=f.name, i=i):
                        g = next(x for x in p.funcs if x.name == name)
                        if len(g.inputs) < 2:
                            return False
                        del g.inputs[i]
                    out.append(drop_input)
        return out

    @staticmethod
    def fewer_statements(prog):
        out = []
        for bi, b in enumerate(gen.all_blocks(prog)):
            keeps = sum(1 for s in b.stmts if s.op == 'ret' and returns_value(prog, b))
            if len(b.stmts) - keeps > 1:
                def clear(p, bi=bi):
                    blk = gen.all_blocks(p)[bi]
                    blk.stmts = [s for s in blk.stmts if s.op == 'ret' and returns_value(p, blk)]
                out.append(clear)
            for si, s in enumerate(b.stmts):
                if s.op == 'ret' and returns_value(prog, b):
                    continue    # a non-void function must still end in its return

                def drop(p, bi=bi, si=si):
                    del gen.all_blocks(p)[bi].stmts[si]
                out.append(drop)
        return out

    @staticmethod
    def flatter(prog):
        """Replace a compound statement by one of its blocks. A block that
        jumps is only lifted to the end of its parent, so the edit never
        leaves unreachable code behind it."""
        out = []
        for bi, b in enumerate(gen.all_blocks(prog)):
            for si, s in enumerate(b.stmts):
                for k, sub in enumerate(s.b):
                    if sub is None:
                        continue
                    if s.op == 'block' and sub.decls:
                        continue
                    jumps = any(x.op in ('ret', 'break', 'continue') for x in sub.stmts)
                    if jumps and si != len(b.stmts) - 1:
                        continue

                    def lift(p, bi=bi, si=si, k=k):
                        blk = gen.all_blocks(p)[bi]
                        inner = blk.stmts[si].b[k]
                        if inner.decls:
                            blk.stmts[si] = gen.S('block', b=[inner])
                        else:
                            blk.stmts[si:si + 1] = inner.stmts
                    out.append(lift)
        return out

    @staticmethod
    def simpler_expressions(prog):
        out = []
        for i, (slot, parent, index) in enumerate(parented_slots(prog)):
            node = gen.slot_get(slot)
            t = gen.ty(node)
            if not gen.is_int(t) or is_lvalue_slot(parent, index) or node.op == 'const':
                continue
            options = []
            # a cast is only ever dropped ('plain'): replacing it by a cast of
            # its operand would be the same program again
            if node.op not in ('idx', 'fld', 'pidx', 'deref', 'addr', 'cast'):
                for k, kid in enumerate(node.k):
                    if node.op in ('asg', 'incdec') and k == 0:
                        continue
                    if gen.is_int(gen.ty(kid)):
                        options.append(('kid', k))
            options += [('const', 1), ('const', 0)]
            if node.op == 'cast' or (node.op == 'asg' and node.a != '='):
                options.append(('plain', 0))
            for what, k in options:
                def mutate(p, i=i, what=what, k=k):
                    slot = parented_slots(p)[i][0]
                    node = gen.slot_get(slot)
                    t = gen.ty(node)
                    if what == 'kid':
                        new = node.k[k]
                        if gen.ty(new) is not t:
                            new = gen.E('cast', [new], t=t)
                    elif what == 'const':
                        ct = t if t.bits >= 16 else (gen.I16 if t.signed else gen.U16)
                        new = gen.mk_const(k, ct)
                    elif node.op == 'cast':
                        new = node.k[0]
                    else:
                        node.a = '='
                        return None
                    gen.slot_set(slot, new)
                out.append(mutate)
        return out

    @staticmethod
    def without_unused(prog):
        def mutate(p):
            used = referenced(p)
            n = 0
            for b in gen.all_blocks(p):
                keep = [d for d in b.decls if d in used]
                n += len(b.decls) - len(keep)
                b.decls = keep
            keep = [g for g in p.globals if g in used]
            n += len(p.globals) - len(keep)
            p.globals = keep
            called = set()
            for slot in gen.expr_slots(p):
                e = gen.slot_get(slot)
                if e.op == 'call':
                    called.add(e.a)
            keep = [f for f in p.funcs if f.kind == 'test' or f in called]
            n += len(p.funcs) - len(keep)
            p.funcs = keep
            return None if n else False
        return [mutate]

    @staticmethod
    def fewer_params(prog):
        out = []
        used = referenced(prog)
        for f in prog.funcs:
            if f.kind == 'rec':
                continue
            for pi, param in enumerate(f.params):
                if param in used:
                    continue

                def drop(p, name=f.name, pi=pi):
                    g = next(x for x in p.funcs if x.name == name)
                    del g.params[pi]
                    for args in g.inputs:
                        del args[pi]
                    for slot in gen.expr_slots(p):
                        e = gen.slot_get(slot)
                        if e.op == 'call' and e.a is g:
                            del e.k[pi]
                out.append(drop)
        return out

    @staticmethod
    def plainer_decls(prog):
        """Initialize a local with a constant instead of an expression."""
        out = []
        for bi, b in enumerate(gen.all_blocks(prog)):
            for di, d in enumerate(b.decls):
                if isinstance(d.init, gen.E) and d.init.op != 'const' and gen.is_int(d.t):
                    def plain(p, bi=bi, di=di):
                        dd = gen.all_blocks(p)[bi].decls[di]
                        ct = dd.t if dd.t.bits >= 16 else (gen.I16 if dd.t.signed else gen.U16)
                        dd.init = gen.mk_const(1, ct)
                    out.append(plain)
        return out

    def run(self):
        if not self.still_fails(copy.deepcopy(self.prog)):
            return False
        passes = [self.only_tests, self.fewer_tests, self.fewer_statements, self.flatter,
                  self.without_unused, self.fewer_params, self.plainer_decls,
                  self.simpler_expressions, self.without_unused]
        for round_ in range(8):
            changed = False
            for p in passes:
                changed |= self.sweep(p)
                if self.args.verbose:
                    print('  round %d %s: %d lines, %d tests run' % (
                        round_, p.__name__, len(gen.render(self.prog).splitlines()), self.tests))
                    sys.stdout.flush()
            if not changed:
                break
        return True


def reduce_seed(seed, cfg_name, args):
    cfg = CONFIGS[cfg_name]
    work = args.work / ('%s-reduce-%s' % (seed, cfg_name))
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    prog = make_program(seed, args)
    expected, reason = host_oracle(prog, work / 'orig')
    if expected is None:
        return seed, cfg_name, None, 'program is not usable: %s' % reason
    kind, detail, output = dcc_run(work / 'orig' / 't.c', work / 'orig' / 'dcc', cfg, args)
    kind, detail = judge(kind, detail, output, expected)
    if kind == 'ok':
        return seed, cfg_name, None, 'does not fail under %s' % cfg_name
    reducer = Reducer(prog, cfg, signature(kind, detail), args, work)
    if not reducer.run():
        return seed, cfg_name, None, 'failure is not stable'
    final = copy.deepcopy(reducer.prog)
    expected, reason = host_oracle(final, work / 'final', repair=False)
    if expected is None:
        return seed, cfg_name, None, 'reduced program is not usable: %s' % reason
    out = work / 'reduced.c'
    out.write_text(gen.render(final, title=', seed %d reduced under %s' % (seed, cfg_name)))
    (work / 'reduced.expected').write_text(expected)
    kind2, detail2, output = dcc_run(out, work / 'final' / 'dcc', cfg, args)
    (work / 'reduced.actual').write_text(output)
    kind2, detail2 = judge(kind2, detail2, output, expected)
    for sub in work.glob('try*'):
        shutil.rmtree(sub, ignore_errors=True)
    known = ' [already has: %s]' % ','.join(sorted(reducer.shapes)) if reducer.shapes else ''
    return seed, cfg_name, out, '%s %s (%d lines, %d tests)%s' % (
        kind2, detail2, len(out.read_text().splitlines()), reducer.tests, known)


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
    parser.add_argument('--configs', default='default',
                        help='production, default (production + no exact schedules), all, '
                             'or a comma-separated list of config names')
    parser.add_argument('--tests', type=int, default=6, help='test functions per program')
    parser.add_argument('--size', type=int, default=10, help='statements per test function')
    parser.add_argument('--depth', type=int, default=3, help='statement nesting depth')
    parser.add_argument('--off', default='',
                        help='comma-separated generator features to leave out; `known` '
                             'leaves out every form that hits a logged dcc bug. Features: %s'
                             % ', '.join(sorted(gen.FEATURES)))
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    parser.add_argument('--reduce-jobs', type=int, default=16,
                        help='candidate edits tried at once per reduction')
    parser.add_argument('--work', type=Path, default=REPO / 'build' / 'clang-oracle-fuzz')
    parser.add_argument('--emulator', default='ntvcm')
    parser.add_argument('--stack', type=int, default=4096, help='target stack bytes')
    parser.add_argument('--timeout', type=int, default=20, help='seconds to run a program')
    parser.add_argument('--build-timeout', type=int, default=120)
    parser.add_argument('--keep', action='store_true', help='keep passing seeds too')
    parser.add_argument('--verbose', action='store_true')
    parser.add_argument('--list-configs', action='store_true')
    parser.add_argument('--generate', nargs=2, metavar=('SEED', 'C'),
                        help='write one repaired program and FILE.expected beside it')
    parser.add_argument('--reduce', help='seeds whose failure to reduce')
    parser.add_argument('--config', default='nopeep', help='config to reduce under')
    args = parser.parse_args()
    args.off = frozenset(x for x in args.off.split(',') if x)
    if 'known' in args.off:
        args.off = (args.off - {'known'}) | frozenset(gen.KNOWN_BUGS)
    unknown = args.off - gen.FEATURES
    if unknown:
        parser.error('unknown feature: %s' % ', '.join(sorted(unknown)))
    if args.list_configs:
        for c in CONFIGS.values():
            print('%-48s %s' % (c.name, ' '.join('%s=%s' % kv for kv in c.env.items())))
        return 0
    if args.generate:
        seed, path = int(args.generate[0]), Path(args.generate[1])
        work = args.work / ('%d-generate' % seed)
        prog = make_program(seed, args)
        expected, reason = host_oracle(prog, work)
        if expected is None:
            print('seed %d is not usable: %s' % (seed, reason))
            return 1
        path.write_text(gen.render(prog, title=', seed %d' % seed))
        path.with_suffix('.expected').write_text(expected)
        shutil.rmtree(work, ignore_errors=True)
        return 0
    if args.reduce:
        seeds = parse_seeds(args.reduce)
        outer = max(1, args.jobs // args.reduce_jobs)
        with concurrent.futures.ThreadPoolExecutor(max_workers=outer) as pool:
            for seed, cfg, path, note in pool.map(
                    lambda s: reduce_seed(s, args.config, args), seeds):
                print('seed %d %s: %s%s' % (seed, cfg, note, ' -> %s' % path if path else ''))
                sys.stdout.flush()
        return 0
    return fuzz(args)


if __name__ == '__main__':
    sys.exit(main())
