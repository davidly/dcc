#!/usr/bin/env python3
"""Compare linked CP/M fixed-decimal formatting of exact float bits to clang.

%e/%g are deliberately checked as unsupported, literal-emitting conversions.
All artifacts stay under build/; the default 6000-pattern table fits CP/M RAM.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import random
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def patterns() -> list[int]:
    rng = random.Random(60)
    values = {0, 0x80000000, 0x41649249, 0xC0B55555, 0x40B55555,
              0x3FC00000, 0x7F7FFFFF, 0xFF7FFFFF, 0x00800000, 0x80800000,
              1, 0x80000001, 0x007FFFFF, 0x807FFFFF}
    for exponent in range(255):
        mantissas = (0, 1, 0x3FFFFF, 0x7FFFFE, 0x7FFFFF,
                     rng.randrange(1 << 23), rng.randrange(1 << 23))
        for mantissa in mantissas:
            for sign in (0, 0x80000000):
                values.add(sign | exponent << 23 | mantissa)
    boundaries = [0.5, 1.5, 2.5, 3.5, 4.5, 7.5, 99.5, 999.5,
                  0.999999, 0.9999995, 9.999995, 99.99995]
    boundaries += [10.0 ** exponent for exponent in range(-38, 39)]
    for value in boundaries:
        bits = struct.unpack('<I', struct.pack('<f', value))[0]
        for offset in range(-2, 3):
            for sign in (0, 0x80000000):
                values.add(sign | (bits + offset))
    while len(values) < 6000:
        values.add(rng.randrange(255) << 23 | rng.randrange(1 << 23)
                   | rng.randrange(2) << 31)
    return sorted(values)


def source(values: list[int], precisions: tuple[int, ...], unsupported: bool) -> str:
    formats = ['%08lx'] + (['%f'] if unsupported else [])
    formats += ['%.' + str(precision) + 'f' for precision in precisions]
    if unsupported:
        formats += ['%e', '%g']
    arguments = ',b.f' * (len(formats) - 1)
    table = ',\n'.join('0x%08xUL' % value for value in values)
    return '''#include <stdio.h>
#include <stdint.h>
typedef union { uint32_t u; float f; } bits;
static const uint32_t patterns[] = {\n''' + table + '''\n};
int main(void) { unsigned int i; bits b;
for(i=0;i<''' + str(len(values)) + ''';i++) { b.u=patterns[i];
printf("''' + '|'.join(formats) + '''\\n", (unsigned long)b.u''' + arguments + ''');
}return 0;}
'''


def prove(work: Path, name: str, values: list[int], precisions: tuple[int, ...],
          unsupported: bool, runtime: str) -> None:
    program = work / (name + '.c')
    program.write_text(source(values, precisions, unsupported))
    host = work / (name + '-host')
    subprocess.run(['clang', '-O2', str(program), '-o', str(host)],
                   cwd=ROOT, check=True)
    expected = subprocess.run([str(host.resolve())], stdout=subprocess.PIPE,
                              text=True, timeout=30, check=True).stdout
    (work / (name + '-host.txt')).write_text(expected)
    for peep in ('false', 'true'):
        subprocess.run(['./dccmake', str(program), 'dcc-output=' + name,
                        'dcc-peep=' + peep, 'dcc-build-dir=' + str(work),
                        'dcc-runtime=' + runtime], cwd=ROOT, check=True)
        actual = subprocess.run(['ntvcm', '-s:0', str(work / (name.upper() + '.COM'))],
                                cwd=ROOT, stdout=subprocess.PIPE, text=True,
                                timeout=180, check=True).stdout
        (work / (name + '-' + peep + '.txt')).write_text(actual)
        want, got = expected.splitlines(), actual.splitlines()
        if len(want) != len(values) or len(got) != len(values):
            raise AssertionError((name, 'line count', len(want), len(got)))
        mismatches = []
        for host_line, target_line in zip(want, got):
            host_fields, target_fields = host_line.split('|'), target_line.split('|')
            if unsupported:
                if target_fields[-2:] != ['e', 'g']:
                    raise AssertionError(('unsupported conversion changed', target_line))
                host_fields, target_fields = host_fields[:-2], target_fields[:-2]
            if host_fields != target_fields:
                mismatches.append((host_line, target_line))
        (work / (name + '-' + peep + '-diff.json')).write_text(
            json.dumps(mismatches, indent=2))
        count = len(values) * (len(precisions) + int(unsupported))
        print(f'{name} peep={peep}: {len(values)} patterns, {count} fixed '
              f'conversions, {len(mismatches)} mismatches', flush=True)
        if mismatches:
            raise AssertionError(mismatches[:8])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, default=Path('build/printf-float-proof'))
    parser.add_argument('--runtime', default='DCCRTL.MAC')
    args = parser.parse_args()
    if not args.work.resolve().is_relative_to((ROOT / 'build').resolve()):
        parser.error('--work must be inside the repository build directory')
    args.work.mkdir(parents=True, exist_ok=True)
    values = patterns()
    prove(args.work, 'F1FIXED', values, tuple(range(7)), True, args.runtime)
    # Include both signs, both zeroes, normal/subnormal extremes, and interiors.
    deep = sorted(set(values[::30] + values[:12] + values[-12:]
                      + [0x80000000, 0x3FC00000, 0x41649249, 0xC0B55555]))
    prove(args.work, 'F1DEEP', deep, (7, 9, 16, 32, 64, 149, 150, 255),
          False, args.runtime)


if __name__ == '__main__':
    main()
