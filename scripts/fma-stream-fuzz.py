#!/usr/bin/env python3
"""Differential float multiply-add streaming tests (Python 3.7+).

Run after building the current compiler:
  python3 scripts/fma-stream-fuzz.py --seeds 1000 --jobs 8

An isolated control compiler disables only mir_float_madd_stream_loads.
Both sides use the current runtime. Programs print every pool's raw float
bits plus side effects and frame guards, rather than relying on float printf
or a host's different floating-point rules. Four builds cover peephole on/off
and stack checks on/off. Sources and a JSON report are retained; failing
builds retain assembly, executables, logs and output. --keep-build keeps all.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import random
import re
import shutil
import sys
import time

from process_supervision import Process

ROOT = Path(__file__).resolve().parents[1]
WIDE_LOAD = ('ld e,(hl)', 'inc hl', 'ld d,(hl)', 'inc hl', 'ld a,(hl)',
             'inc hl', 'ld h,(hl)', 'ld l,a', 'ex de,hl')
FAMILIES = ('plain', 'strides', 'calls', 'branch', 'volatile', 'reuse',
            'pressure', 'frame', 'multiple', 'matrix', 'indexcall', 'absolute')


def run(argv, cwd, log, timeout, environment=None):
    result = Process(argv, cwd, log=log, environment=environment,
                     remove_environment=('DCC_FORCE_STACK_CHECK',
                                         'DCC_MIR_SELECT_CANDIDATE',
                                         'DCC_MIR_SELECT_FUNCTION')).complete(timeout)
    if result['TimedOut'] or result['ExitCode']:
        raise RuntimeError('%s: %s (see %s)' %
                           (Path(str(argv[0])).name,
                            'timeout' if result['TimedOut'] else result['ExitCode'], log))
    return result['Output'].replace('\r', '')


def build_control(work, jobs):
    base = work / 'control-source'
    if base.exists():
        raise RuntimeError('control-source already exists; choose a fresh --work directory')
    (base / 'src').mkdir(parents=True)
    shutil.copytree(str(ROOT / 'src/dcc'), str(base / 'src/dcc'))
    (base / 'scripts').mkdir()
    shutil.copy2(str(ROOT / 'scripts/generate-version.py'), str(base / 'scripts'))
    source = base / 'src/dcc/dcc_mir_spilled_cfg.c'
    text = source.read_text()
    signature = 'static int mir_float_madd_stream_loads(int add_index, int loads[3])\n{'
    if text.count(signature) != 1:
        raise RuntimeError('streaming selector signature changed')
    # This guard is the sole semantic difference between the two compilers.
    source.write_text(text.replace(signature, signature + '\n    return 0; /* fuzz control */', 1))
    binary = work / 'control-bin'
    run(['cmake', '-S', str(base / 'src/dcc'), '-B', str(work / 'control-build'),
         '-DCMAKE_BUILD_TYPE=Release', '-DDCC_RUNTIME_OUTPUT_DIRECTORY=' + str(binary)],
        work, work / 'control-configure.log', 120)
    run(['cmake', '--build', str(work / 'control-build'), '--parallel', str(jobs)],
        work, work / 'control-build.log', 300)
    return binary / ('dcc.exe' if os.name == 'nt' else 'dcc')


def program(seed):
    rng = random.Random(seed)
    # Independent bounded index expressions, including helper-based integer
    # division/modulo and multi-dimensional strides between float loads.
    def index():
        return '((i * %d + %d) & 15)' % (rng.choice((1, 3, 5, 7)), rng.randrange(16))
    x, y, z = index(), index(), index()
    steps = rng.randrange(8, 25)
    body = [
        'd[%s] += a[%s] * b[%s];' % (x, y, z),
        'd[%s] += a[(i * %d / 3) & 15] * b[(i * %d %% 7) & 15];' %
        (x, rng.randrange(1, 8), rng.randrange(1, 8)),
        'd[%s] += a[%s] * fetch(b, %s);' % (x, y, z),
        'd[%s] += a[%s] * ((i & 1) ? b[%s] : b[%s]);' % (x, y, z, y),
        '{ volatile float *v = b; d[%s] += a[%s] * v[%s]; }' % (x, y, z),
        '{ float v = a[%s]; d[%s] += v * (v + b[%s]); }' % (y, x, z),
        ('{ int p = (i * 7) & 15; int q = (i + 9) & 15; float v = a[p]; '
         'd[%s] += a[%s] * b[%s]; d[q] = d[q] + v; effects += p + q; }') % (x, y, z),
        ('{ float frame[48]; int j; for (j=0; j<48; ++j) frame[j]=(float)(j-12); '
         'frame[32 + (i & 7)] += a[%s] * b[%s]; '
         'd[%s] = frame[32 + (i & 7)]; '
         'if (frame[0] != -12.0f || frame[47] != 35.0f) guards++; }') % (y, z, x),
        'd[%s] += a[%s] * b[%s]; d[%s] += a[%s] * b[%s];' % (x, y, z, z, x, y),
        ('{ int j,k; for(j=0;j<4;++j) for(k=0;k<4;++k) '
         'd[(i&3)*4+j] += a[(i&3)*4+k] * b[k*4+j]; }'),
        'd[%s] += a[%s] * b[choose(%s)];' % (x, y, z),
        'd[5] += a[2] * b[11];'
    ]
    functions = []
    for number, code in enumerate(body):
        functions.append('void f%d(float *a, float *b, float *d, int i) { %s effects += (a == b) + (b == d); }' % (number, code))
    patterns = [0, 0x80000000, 0x3f800000, 0xbf800000, 0x40000000,
                0xc0400000, 0x3fc00000, 0x7f800000, 0xff800000,
                0x7fc00001, 0x7f7fffff, 0x00800000, 1, 0x80000001]
    initial = []
    for i in range(64):
        bits = rng.choice(patterns) if i % 3 == 0 else rng.getrandbits(32)
        # Retain diverse finite values without every workload immediately
        # overflowing. Every third element still samples IEEE edge cases.
        if i % 3:
            bits = (bits & 0x807fffff) | (rng.randrange(120, 135) << 23)
        initial.append('0x%08xUL' % bits)
    lines = ['#include <stdio.h>', '#include <stdint.h>',
             'union Bits { float f; uint32_t u; };',
             'float pool[64]; unsigned int effects, guards;',
             'uint32_t initial[64] = { %s };' % ','.join(initial),
             'float fetch(float *p,int i) { effects++; return p[i]; }',
             'int choose(int i) { effects += 3; return i; }']
    lines += functions
    lines += ['int main(void) { int f,i,j; union Bits v;',
              'for(f=0;f<12;++f) {',
              'effects=0; guards=0;',
              'for(j=0;j<64;++j) { v.u=initial[j]; pool[j]=v.f; }',
              'for(i=0;i<%d;++i) {' % steps, 'switch(f) {']
    # Offset views exercise disjoint, exact and partial overlap. The largest
    # index is 15; every base offset remains within the 64-element pool.
    offsets = [(0,16,32), (0,16,0), (0,0,0), (0,1,2), (16,0,8), (1,17,33)]
    for number in range(12):
        a,b,d = rng.choice(offsets)
        lines.append('case %d: f%d(pool+%d,pool+%d,pool+%d,i); break;' %
                     (number, number, a,b,d))
    lines += ['} }', 'printf("%d %u %u\\n",f,effects,guards);',
              'for(j=0;j<64;++j) { v.f=pool[j]; printf("%08lx\\n",(unsigned long)v.u); }',
              '} return 0; }']
    return '\n'.join(lines) + '\n'


def stream_loads(assembly):
    instructions = [line.split(';', 1)[0].strip().lower() for line in assembly.splitlines()]
    instructions = [line for line in instructions if line]
    return sum(tuple(instructions[i:i+len(WIDE_LOAD)]) == WIDE_LOAD
               for i in range(len(instructions)))


def build_run(source, directory, compiler, peep, stack, args):
    directory.mkdir(parents=True)
    command = [str(ROOT / ('dccmake.exe' if os.name == 'nt' else 'dccmake')),
               '--no-config', 'dcc-input=' + str(source), 'dcc-output=T',
               'dcc-build-dir=' + str(directory), 'dcc-tool=' + str(compiler),
               'dcc-include-directory=' + str(ROOT),
               'dcc-peep=' + str(peep).lower(), 'dcc-stack-check=' + str(stack).lower(),
               'dcc-stack-bytes=2048', 'dcc-flongio=true']
    run(command, ROOT, directory / 'build.log', args.build_timeout,
        {'DCC_MIR_SELECT_CANDIDATE': 'spilled-phi-slot'} if args.force_spilled else None)
    output = run([args.emulator, '-s:0', 'T.COM'], directory,
                 directory / 'run.log', args.timeout)
    assembly = (directory / 'T.MAC').read_text()
    counts = {}
    for number, family in enumerate(FAMILIES):
        match = re.search(r';@dcc.lto begin _f%d\s(.*?);@dcc.lto end _f%d\b' %
                          (number, number), assembly, re.S)
        counts[family] = stream_loads(match.group(1)) if match else 0
    return output, counts


def check_seed(seed, control, args):
    directory = args.work / ('seed-%06d' % seed)
    directory.mkdir()
    source = directory / 'case.c'
    source.write_text(program(seed))
    result = {'seed': seed, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
              'builds': 0, 'active_variants': 0, 'active_families': [], 'outputs': {}}
    try:
        for peep in (False, True):
            for stack in (False, True):
                label = 'p%d-s%d' % (peep, stack)
                old, old_loads = build_run(source, directory / (label + '-control'),
                                          control, peep, stack, args)
                new, new_loads = build_run(source, directory / (label + '-stream'),
                                          args.compiler, peep, stack, args)
                result['builds'] += 2
                active = [family for family in FAMILIES if new_loads[family] > old_loads[family]]
                result['active_variants'] += bool(active)
                result['active_families'] = sorted(set(result['active_families']) | set(active))
                forbidden = set(active) & {'calls', 'branch', 'volatile', 'reuse', 'indexcall'}
                if forbidden:
                    raise RuntimeError('streaming used in fallback families: ' + ','.join(sorted(forbidden)))
                result['outputs'][label] = hashlib.sha256(new.encode()).hexdigest()
                if old != new:
                    raise RuntimeError('output mismatch: ' + label)
                # Independently check guards instead of trusting control.
                headers = re.findall(r'^\d+ \d+ (\d+)$', new, re.M)
                if len(headers) != 12 or any(int(x) for x in headers):
                    raise RuntimeError('missing family output or damaged frame guard: ' + label)
        if not result['active_variants']:
            raise RuntimeError('no streaming activation in any build')
        result['passed'] = True
        if not args.keep_build:
            for child in directory.iterdir():
                if child.is_dir():
                    shutil.rmtree(str(child))
    except Exception as error:
        result['passed'] = False
        result['error'] = str(error)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seeds', type=int, default=100)
    parser.add_argument('--start', type=int, default=1)
    parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument('--compiler', type=Path, default=ROOT / ('dcc.exe' if os.name == 'nt' else 'dcc'))
    parser.add_argument('--control-compiler', type=Path)
    parser.add_argument('--work', type=Path, default=ROOT / 'build' / ('fma-stream-fuzz-%d' % int(time.time())))
    parser.add_argument('--emulator', default='ntvcm')
    parser.add_argument('--build-timeout', type=float, default=30)
    parser.add_argument('--timeout', type=float, default=5)
    parser.add_argument('--keep-build', action='store_true')
    parser.add_argument('--force-spilled', action='store_true',
                        help='also exercise the spilled backend when normal selection prefers another')
    args = parser.parse_args()
    if min(args.seeds, args.start, args.jobs) < 1:
        parser.error('seeds, start and jobs must be positive')
    args.work = args.work.resolve()
    args.compiler = args.compiler.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    control = args.control_compiler.resolve() if args.control_compiler else build_control(args.work, args.jobs)
    results = []
    started = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(check_seed, seed, control, args)
                   for seed in range(args.start, args.start + args.seeds)]
        for future in concurrent.futures.as_completed(futures):
            result = future.result()
            results.append(result)
            if not result['passed'] or len(results) % 25 == 0 or len(results) == args.seeds:
                print('%d/%d seeds; %d failures%s' %
                      (len(results), args.seeds, sum(not r['passed'] for r in results),
                       ': seed %d %s' % (result['seed'], result.get('error', ''))
                       if not result['passed'] else ''), flush=True)
    summary = {'seeds': args.seeds, 'families': FAMILIES,
               'builds': sum(r['builds'] for r in results),
               'active_variants': sum(r['active_variants'] for r in results),
               'force_spilled': args.force_spilled,
               'active_families': sorted(set(f for r in results for f in r['active_families'])),
               'failures': sum(not r['passed'] for r in results),
               'elapsed_seconds': round(time.monotonic() - started, 3),
               'candidate_sha256': hashlib.sha256(args.compiler.read_bytes()).hexdigest(),
               'control_sha256': hashlib.sha256(control.read_bytes()).hexdigest(),
               'runtime_sha256': hashlib.sha256((ROOT / 'DCCRTL.MAC').read_bytes()).hexdigest(),
               'results': sorted(results, key=lambda r: r['seed'])}
    (args.work / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print('Report: %s' % (args.work / 'summary.json'))
    return bool(summary['failures'])


if __name__ == '__main__':
    sys.exit(main())
