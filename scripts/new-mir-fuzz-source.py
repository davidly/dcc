#!/usr/bin/env python3
"""Generate deterministic 16-bit MIR fuzz programs and their oracle (Python 3.7+)."""
import argparse
from pathlib import Path

SAMPLES = (0,1,127,255,256,32767,32768,65535)


def generate(seed=23117, programs=12):
    if not 0 <= seed <= 65535 or not 1 <= programs <= 32:
        raise ValueError('seed must be 0..65535 and programs must be 1..32')
    state = seed
    def choice(limit):
        nonlocal state
        state = (state*25173+13849)&65535
        return state%limit
    lines = ['#include <stdio.h>','#ifndef FUZZ_MUTATE','#define FUZZ_MUTATE 0','#endif',
        'unsigned int touch8(unsigned char *target, unsigned char *alias, unsigned int value) { *target = (unsigned char)(*target ^ value); return *alias; }',
        'unsigned int touch16(unsigned int *target, unsigned int *alias, unsigned int value) { *target ^= value; return *alias; }',
        'unsigned int alter8(unsigned char *target, unsigned char *alias, unsigned int value) { *alias = (unsigned char)(*alias + value); return *target; }',
        'unsigned int alter16(unsigned int *target, unsigned int *alias, unsigned int value) { *alias += value; return *target; }']
    expected = []
    for program in range(programs):
        width = 8 if program%2 == 0 else 16
        mask = (1<<width)-1
        element = 'unsigned char' if width == 8 else 'unsigned int'
        operations = [(choice(5),1+choice(255),choice(8)) for _ in range(6)]
        lines += ['unsigned int fuzz{}(unsigned int seed) {{'.format(program),
            '{} data[4]; {} *alias; unsigned int first, second, saved, count; int slot;'.format(element,element),
            'unsigned int (*callback)({0} *, {0} *, unsigned int) = (seed & 64U) ? touch{1} : alter{1};'.format(element,width),
            'for (slot = 0; slot < 4; ++slot) data[slot] = seed + (unsigned int)slot * 17U;',
            'alias = (seed & 1U) ? &data[1] : &data[2]; first = seed; second = seed ^ 43690U; saved = data[1];']
        for kind,constant,shift in operations:
            expression = ('first + {}U'.format(constant),'first ^ {}U'.format(constant),
                'first * {}U'.format(constant),'first >> {}'.format(shift),'first / {}U'.format(constant))[kind]
            lines += ['first = (unsigned int)({});'.format(expression),'second += callback(&data[1], alias, first);']
        lines += ['for (count = 0; count < (seed & 3U); ++count) { first += data[count]; second ^= first; }',
            'if (seed & 128U) first ^= second; else first += second;',
            'return (unsigned int)(first + saved + data[1] + data[2]) ^ FUZZ_MUTATE; }']
        for sample in SAMPLES:
            data = [(sample+slot*17)&mask for slot in range(4)]
            alias = 1 if sample&1 else 2
            first,second,saved = sample,sample^43690,data[1]
            for kind,constant,shift in operations:
                # Integer arithmetic, never host float division or host C widths.
                if kind == 0: first = (first+constant)&65535
                elif kind == 1: first ^= constant
                elif kind == 2: first = (first*constant)&65535
                elif kind == 3: first >>= shift
                else: first //= constant
                if sample&64:
                    data[1] = (data[1]^first)&mask
                    callback = data[alias]
                else:
                    data[alias] = (data[alias]+first)&mask
                    callback = data[1]
                second = (second+callback)&65535
            for count in range(sample&3):
                first = (first+data[count])&65535
                second ^= first
            first = first^second if sample&128 else (first+second)&65535
            expected.append((first+saved+data[1]+data[2])&65535)
    lines += ['static unsigned int inputs[8] = { '+','.join(map(str,SAMPLES))+' };',
        'static unsigned int expected[{}] = {{ {} }};'.format(len(expected),','.join(map(str,expected))),
        'static int failures, checks;',
        'static void check(unsigned int actual) { if (actual != expected[checks]) { ++failures; printf("FAIL fuzz check=%d got=%u expected=%u\\n", checks, actual, expected[checks]); } ++checks; }',
        'int main(void) { int sample;']
    for program in range(programs):
        lines.append('for (sample = 0; sample < 8; ++sample) check(fuzz{}(inputs[sample]));'.format(program))
    lines.append('printf("MIR fuzz seed={} checks=%d failures=%d\\n", checks, failures); return failures != 0; }}'.format(seed))
    return '\n'.join(lines)+'\n'


def bounded(low,high):
    def parse(value):
        number = int(value)
        if not low <= number <= high:
            raise argparse.ArgumentTypeError('must be {}..{}'.format(low,high))
        return number
    return parse


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output-path','-OutputPath',required=True,type=Path)
    p.add_argument('--seed','-Seed',type=bounded(0,65535),default=23117)
    p.add_argument('--programs','-Programs',type=bounded(1,32),default=12)
    args = p.parse_args(argv)
    args.output_path.write_text(generate(args.seed,args.programs),encoding='ascii')


if __name__=='__main__':
    main()
