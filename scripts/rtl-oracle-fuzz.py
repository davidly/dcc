#!/usr/bin/env python3
"""Differential fuzzer for dcc's runtime library, using Python 3.7+.

Each seed generates one C program of independent tests, each a call or a
short sequence of calls into the runtime library with random arguments:

  sprintf/snprintf  formats with flags, widths, precisions and l
  sscanf            integer, %c and %s conversions, widths, literals, *
  strtol/strtoul    signs, prefixes, bases, overflow, end pointers
  atoi/atol/strtod  decimal text, exponents, inf and nan
  string.h          compares, searches, copies, overlapping moves, strtok
  ctype.h           every classification and case mapping, and EOF
  qsort/bsearch     ints, strings and odd-sized records
  math.h            exact functions bit for bit, transcendental ones within
                    the documented 5-6 significant digits
  malloc family     allocate, grow, shrink, free and verify contents
  div/ldiv/abs      quotients and remainders
  va_list printf    vsprintf, vsnprintf, vprintf, vfprintf, and printf,
                    fprintf and puts to stdout
  file I/O          create, reopen in every mode, then random reads, writes,
                    seeks, pushback and status checks against a model of the
                    file; verify, rename and remove; tmpfile
  time.h            gmtime, asctime, strftime, difftime, mktime round trips
                    and normalization
  properties        rand repeatability and range, setjmp/longjmp from nested
                    calls, atexit order, multibyte round trips

File tests stay inside each file's logical length, or use lengths that are a
multiple of 128: CP/M pads the last record with Ctrl-Z, which a read past the
logical end returns and SEEK_END on a reopened file counts. The host runs in
UTC, as dcc has no time zone.

The program is compiled by dcc and run under the emulator, and compiled by
clang with -m32 (so long is 32 bits) and run on the host. Tests print through
putchar only, so a wrong printf cannot hide another result, and they are
independent: a mismatching line names the failing call. Results that depend
on dcc's 16-bit int stay in range on both sides.

  rtl-oracle-fuzz.py --seeds 1-200
  rtl-oracle-fuzz.py --generate 17 prog.c          only write seed 17's program
  rtl-oracle-fuzz.py --generate 17 prog.c --only 5,9
"""
import argparse
import concurrent.futures
import math
import os
from pathlib import Path
import random
import re
import shutil
import struct
import subprocess
import sys

REPO = Path(__file__).resolve().parent.parent
HOST_FLAGS = ['-m32', '-std=gnu99', '-w', '-fsigned-char', '-ffp-contract=off', '-fno-builtin']

PRELUDE = r'''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <setjmp.h>
#include <time.h>
#ifndef _DCC_
#include <strings.h>
#define stricmp strcasecmp
/* dcc's strtod and atof produce float; round once, as it does. */
#define strtod strtof
#endif

static const char hexd[] = "0123456789abcdef";
static void ps(const char *s) { while (*s) putchar(*s++); }
static void pc(int c)
{
    if (c >= 32 && c < 127 && c != '\\') {
        putchar(c);
    } else {
        putchar('\\');
        putchar(hexd[(c >> 4) & 15]);
        putchar(hexd[c & 15]);
    }
}
static void pstr(const char *s) { putchar('"'); while (*s) pc((unsigned char)*s++); putchar('"'); }
static void pmem(const char *s, int n) { int i; putchar('['); for (i = 0; i < n; i++) pc((unsigned char)s[i]); putchar(']'); }
static void px(uint32_t v) { int i; for (i = 28; i >= 0; i -= 4) putchar(hexd[(int)(v >> i) & 15]); }
static void pd(int32_t v)
{
    char b[12];
    int i = 0;
    uint32_t u;

    if (v < 0) { putchar('-'); u = 0u - (uint32_t)v; } else u = (uint32_t)v;
    do { b[i++] = (char)('0' + (int)(u % 10u)); u /= 10u; } while (u);
    while (i) putchar(b[--i]);
}
static void sp(void) { putchar(' '); }
static void nl(void) { putchar('\n'); }
static void tag(int id, const char *t) { pd(id); sp(); ps(t); ps(": "); }
static uint32_t fbits(float f) { union { float f; uint32_t u; } x; x.f = f; return x.u; }
static float bitsf(uint32_t u) { union { float f; uint32_t u; } x; x.u = u; return x.f; }
static void pf(float f) { px(fbits(f)); }
static int sgn(int v) { return (v > 0) - (v < 0); }
static void pidx(const char *base, const void *p) { pd(p ? (int32_t)((const char *)p - base) : -1); }

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}
static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
static int cmp_rec3(const void *a, const void *b) { return memcmp(a, b, 3); }
struct krec { int key; char tag; };
static int cmp_krec(const void *a, const void *b)
{
    int x = ((const struct krec *)a)->key, y = ((const struct krec *)b)->key;
    return (x > y) - (x < y);
}

static int vsp(char *b, const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vsprintf(b, fmt, ap);
    va_end(ap);
    return r;
}
static int vsnp(char *b, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vsnprintf(b, n, fmt, ap);
    va_end(ap);
    return r;
}
static int vp(const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}
static int vfp(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

static jmp_buf jb;
static void jdeep(int d, int v) { if (d <= 0) longjmp(jb, v); jdeep(d - 1, v); }

static void ptm(const struct tm *t)
{
    pd(t->tm_year); sp(); pd(t->tm_mon); sp(); pd(t->tm_mday); sp(); pd(t->tm_hour); sp();
    pd(t->tm_min); sp(); pd(t->tm_sec); sp(); pd(t->tm_wday); sp(); pd(t->tm_yday); sp();
}

static void atexit1(void) { ps("atexit 1\n"); }
static void atexit2(void) { ps("atexit 2\n"); }

static void fill(char *p, int n, int seed) { int i; for (i = 0; i < n; i++) p[i] = (char)(seed + i * 7); }
static int check(const char *p, int n, int seed)
{
    int i;
    for (i = 0; i < n; i++)
        if (p[i] != (char)(seed + i * 7))
            return 0;
    return 1;
}
'''

WORDS = ['', 'a', 'ab', 'abc', 'abcab', 'b', 'ba', 'cab', 'AbC', 'ABC', 'a b', ' a', 'x,y',
         'hello', 'Hello', 'HELLO', 'he', 'lo', 'll', 'z', 'a,b,,c', ',,', 'aaaa', 'abab', '12',
         '1a2b', 'word list', 'tab\tsep']
ALPHA = 'abcAB ,x'


def c_str(s):
    out = []
    for ch in s:
        if ch == '\t':
            out.append('\\t')
        elif ch == '\n':
            out.append('\\n')
        elif ch in '"\\':
            out.append('\\' + ch)
        else:
            out.append(ch)
    return '"' + ''.join(out) + '"'


def c_int(v):
    return '(-32767-1)' if v == -32768 else str(v)


def c_long(v):
    return '(-2147483647L-1)' if v == -2147483648 else '%dL' % v


def c_float(f):
    """A float literal that names exactly the float f."""
    if math.isnan(f):
        return 'bitsf(0x7fc00000UL)'
    if math.isinf(f):
        return 'bitsf(%s)' % ('0xff800000UL' if f < 0 else '0x7f800000UL')
    if f == 0.0 and math.copysign(1.0, f) < 0:
        return 'bitsf(0x80000000UL)'
    return 'bitsf(0x%08xUL)' % struct.unpack('<I', struct.pack('<f', f))[0]


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


class Gen:
    def __init__(self, seed):
        self.r = random.Random(seed)

    def pick(self, seq):
        return self.r.choice(seq)

    def word(self, maxlen=12):
        if self.r.random() < 0.4:
            return self.pick(WORDS)
        return ''.join(self.pick(ALPHA) for _ in range(self.r.randint(0, maxlen)))

    def i16(self):
        return self.pick((0, 1, -1, 7, -7, 100, -100, 32767, -32768, 255, 256,
                          self.r.randint(-32768, 32767), self.r.randint(-999, 999)))

    def u16(self):
        return self.pick((0, 1, 255, 256, 32767, 32768, 65535, self.r.randint(0, 65535),
                          self.r.randint(0, 999)))

    def i32(self):
        return self.pick((0, 1, -1, 2147483647, -2147483648, 65536, -65536, 100000,
                          self.r.randint(-2**31, 2**31 - 1), self.r.randint(-99999, 99999)))

    def u32(self):
        return self.pick((0, 1, 2**32 - 1, 2**31, 65535, 65536, self.r.randint(0, 2**32 - 1)))

    def real(self, lo=-1e6, hi=1e6):
        """A float32 value, often a short decimal or a near-integer."""
        k = self.r.random()
        if k < 0.15:
            v = self.pick((0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 1.5, 2.5, -2.5, 0.1, 100.0, 1e6,
                           0.001, 123.456, 3.14159, 65535.0, 32768.0))
        elif k < 0.4:
            v = self.r.randint(-9999, 9999) / self.pick((1, 2, 4, 8, 10, 100, 1000))
        elif k < 0.6:
            v = self.r.randint(-99999, 99999) + self.pick((0.0, 0.5, 0.25, 0.75, 0.1))
        else:
            v = self.r.uniform(-1, 1) * 10 ** self.r.uniform(-4, 6)
        v = max(lo, min(hi, v))
        return f32(v)

    # ---------------------------------------------------------- printf

    def printf_conv(self):
        """(format text, C argument or None, rough output length)."""
        kind = self.pick(('d', 'd', 'i', 'u', 'x', 'X', 'o', 'c', 's', 's', 'ld', 'li',
                          'lu', 'lx', 'lX', 'f', 'f', 'f', '%'))
        if kind == '%':
            return '%%', None, 1
        flags = ''
        if kind in ('c', 's'):
            flags = self.pick(('', '', '-'))
        else:
            flags = self.pick(('', '', '', '-', '0', '-0'))
        width = self.pick(('', '', str(self.r.randint(1, 12))))
        prec = ''
        if kind in ('d', 'i', 'u', 'x', 'X', 'o', 'ld', 'li', 'lu', 'lx', 'lX', 's', 'f') \
                and self.r.random() < 0.35:
            prec = '.' + str(self.r.randint(0, 6 if kind == 'f' else 8))
        if kind in ('d', 'i'):
            arg, n = c_int(self.i16()), 6
        elif kind in ('u', 'x', 'X', 'o'):
            arg, n = '%du' % self.u16(), 6
        elif kind == 'c':
            arg, n = str(self.r.randint(33, 126)), 1
        elif kind == 's':
            w = self.word(10)
            arg, n = c_str(w), len(w)
        elif kind in ('ld', 'li'):
            arg, n = c_long(self.i32()), 11
        elif kind in ('lu', 'lx', 'lX'):
            arg, n = '%dUL' % self.u32(), 10
        else:
            arg, n = c_float(self.real(-1e6, 1e6)), 15
        n = max(n, int(width or 0), int(prec[1:] or 0) + 2)
        return '%' + flags + width + prec + kind, arg, n

    def t_sprintf(self, tid):
        fmt, args, total = '', [], 0
        for _ in range(self.r.randint(1, 3)):
            if self.r.random() < 0.5:
                lit = ''.join(self.pick('ab =:,[]') for _ in range(self.r.randint(1, 4)))
                fmt += lit
                total += len(lit)
            text, arg, n = self.printf_conv()
            fmt += text
            total += n
            if arg is not None:
                args.append(arg)
        call_args = ', '.join([c_str(fmt)] + args)
        if self.r.random() < 0.3:
            size = self.r.randint(0, total + 3)
            return ['char b[100];', 'int r;', "memset(b, '#', sizeof b);",
                    'r = snprintf(b, %d, %s);' % (size, call_args),
                    'tag(%d, "snprintf %d");' % (tid, size), 'pd(r); sp(); pmem(b, %d); nl();' % min(size + 2, 100)]
        return ['char b[100];', 'int r;', "memset(b, '#', sizeof b);",
                'r = sprintf(b, %s);' % call_args,
                'tag(%d, "sprintf");' % tid, 'pd(r); sp(); pstr(b); nl();']

    # ---------------------------------------------------------- scanf

    def t_sscanf(self, tid):
        fmt, text, decls, outs, args = '', '', [], [], []
        counted = False                  # a %Nc needs all N characters
        for k in range(self.r.randint(1, 4)):
            sep = self.pick(('', ' ', ',', ':', ' , '))
            if k:
                fmt += sep if sep.strip() else ' '
                text += sep if sep else ' '
            text += self.pick(('', '', ' ', '  ', '\t'))
            kind = self.pick(('d', 'd', 'i', 'u', 'x', 'X', 'o', 'c', 'nc', 's', 'ns',
                              'ld', 'lu', 'lx', 'skip'))
            v = 'v%d' % k
            if kind in ('d', 'i', 'u', 'x', 'X', 'o', 'skip'):
                num = self.r.randint(0, 9999)
                if kind in ('d', 'skip'):
                    tok = self.pick(('', '-', '+')) + str(num)
                elif kind == 'i':
                    tok = self.pick(('', '-')) + self.pick((str(num), '0%o' % num, '0x%x' % num,
                                                           '0X%X' % num))
                elif kind == 'u':
                    tok = str(self.r.randint(0, 65535))
                elif kind in ('x', 'X'):
                    tok = self.pick(('', '0x')) + '%x' % self.r.randint(0, 65535)
                else:
                    tok = '%o' % self.r.randint(0, 65535)
                width = self.pick(('', '', str(self.r.randint(1, 4))))
                if width and 'x' in tok.lower():
                    width = ''           # glibc accepts a bare "0x"; C does not
                if width:
                    # The number fits the width, so no remainder is left to
                    # overflow the next 16-bit conversion.
                    if any(ch.isdigit() for ch in tok[:int(width)]):
                        tok = tok[:int(width)]
                    else:
                        width = ''
                if kind == 'skip':
                    fmt += '%*' + width + 'd'
                else:
                    ctype = 'int' if kind in ('d', 'i') else 'unsigned'
                    decls.append('%s %s = 12345;' % (ctype, v))
                    fmt += '%' + width + kind
                    args.append('&' + v)
                    outs.append('pd((int32_t)%s); sp();' % v if ctype == 'int' else 'pd((int32_t)(uint32_t)%s); sp();' % v)
            elif kind in ('ld', 'lu', 'lx'):
                if kind == 'ld':
                    tok = str(self.r.randint(-99999999, 99999999))
                elif kind == 'lu':
                    tok = str(self.r.randint(0, 2**32 - 1))
                else:
                    tok = '%x' % self.r.randint(0, 2**32 - 1)
                ctype = 'long' if kind == 'ld' else 'unsigned long'
                decls.append('%s %s = 12345;' % (ctype, v))
                fmt += '%' + kind
                args.append('&' + v)
                outs.append('px((uint32_t)%s); sp();' % v)
            elif kind in ('c', 'nc'):
                n = 1 if kind == 'c' else self.r.randint(1, 4)
                tok = ''.join(self.pick('abcxyz') for _ in range(n))
                counted = counted or kind == 'nc'
                decls.append("char %s[6] = \"?????\";" % v)
                fmt += '%c' if kind == 'c' else '%%%dc' % n
                args.append(v)
                outs.append('pmem(%s, 5); sp();' % v)
            else:
                w = self.word(8).replace(' ', '').replace('\t', '').replace(',', '') or 'w'
                width = 11 if kind == 's' else self.r.randint(1, 5)
                tok = w[:width]                  # a remainder would feed the next conversion
                decls.append("char %s[12] = \"??????????\";" % v)
                fmt += '%11s' if kind == 's' else '%%%ds' % width
                args.append(v)
                outs.append('pstr(%s); sp();' % v)
            text += tok
        if self.r.random() < 0.2 and text and not counted:
            i = self.r.randrange(len(text))
            if i == 0 or text[i - 1] not in 'xX':
                text = text[:i] + self.pick('q;') + text[i + 1:]
        if self.r.random() < 0.1 and not counted:
            text = self.pick(('', ' '))
        call = 'n = sscanf(%s, %s%s);' % (c_str(text), c_str(fmt),
                                         ''.join(', ' + a for a in args))
        return decls + ['int n;', call, 'tag(%d, "sscanf");' % tid, 'pd(n); sp();'] + outs + ['nl();']

    # ---------------------------------------------------------- numbers

    def number_text(self, base):
        alphabet = '0123456789abcdefghijklmnopqrstuvwxyz'
        b = base or 10
        digits = alphabet[:b] + alphabet[10:b].upper()
        s = self.pick(('', '', ' ', '\t ', '  '))
        s += self.pick(('', '', '', '-', '+'))
        if base in (0, 16) and self.r.random() < 0.4:
            s += self.pick(('0x', '0X'))
            digits = '0123456789abcdefABCDEF'
        elif base == 0 and self.r.random() < 0.3:
            s += '0'
            digits = '01234567'
        n = self.pick((0, 1, 2, 4, 6, 8, 10, 11, 12, 16, 33))
        s += ''.join(self.pick(digits) for _ in range(n))
        s += self.pick(('', '', '', 'z', ' 5', '9', '.5', 'x'))
        return s

    def t_strtol(self, tid):
        base = self.pick((0, 0, 10, 10, 16, 8, 2, 36, self.r.randint(2, 36)))
        s = self.number_text(base)
        fn = self.pick(('strtol', 'strtoul'))
        ctype = 'long' if fn == 'strtol' else 'unsigned long'
        return ['static const char s[] = %s;' % c_str(s), 'char *e;', '%s v;' % ctype,
                'errno = 0;', 'v = %s(s, &e, %d);' % (fn, base),
                'tag(%d, "%s %d");' % (tid, fn, base),
                'px((uint32_t)v); sp(); pd((int32_t)(e - s)); sp(); pd(errno == ERANGE); nl();']

    def t_atoi(self, tid):
        fn = self.pick(('atoi', 'atol'))
        v = self.r.randint(-99999, 99999) if fn == 'atoi' else self.r.randint(-2**31 + 1, 2**31 - 1)
        s = self.pick(('', ' ', '\t')) + self.pick(('', '+')) + str(v) + self.pick(('', 'x', ' 7'))
        if fn == 'atoi':
            return ['tag(%d, "atoi");' % tid,
                    'pd((int16_t)atoi(%s)); nl();' % c_str(s)]
        return ['tag(%d, "atol");' % tid, 'px((uint32_t)atol(%s)); nl();' % c_str(s)]

    def float_text(self):
        k = self.r.random()
        if k < 0.1:
            body = self.pick(('inf', 'INF', 'infinity', 'nan', 'NaN', 'Infinity', 'in', 'na'))
        else:
            mant = str(self.r.randint(0, 999999))
            if self.r.random() < 0.6:
                i = self.r.randint(0, len(mant))
                mant = mant[:i] + '.' + mant[i:]
            body = mant
            if self.r.random() < 0.4:
                # dcc flushes a subnormal result to zero, as documented; keep
                # negative exponents in the normal range.
                sign = self.pick(('', '-', '+'))
                body += self.pick('eE') + sign + str(self.r.randint(0, 30 if sign == '-' else 40))
        return self.pick(('', ' ')) + self.pick(('', '', '-', '+')) + body + self.pick(('', '', 'x', 'e', ' 1'))

    def t_strtod(self, tid):
        s = self.float_text()
        return ['static const char s[] = %s;' % c_str(s), 'char *e;', 'float v;',
                'errno = 0;', 'v = (float)strtod(s, &e);', 'tag(%d, "^strtod");' % tid,
                'pf(v); sp(); pd((int32_t)(e - s)); sp(); pd(errno == ERANGE); nl();']

    def t_div(self, tid):
        if self.r.random() < 0.5:
            a, b = self.i16(), self.i16() or 3
            if a == -32768 and b == -1:
                b = 7
            return ['div_t q = div(%s, %s);' % (c_int(a), c_int(b)), 'tag(%d, "div");' % tid,
                    'pd(q.quot); sp(); pd(q.rem); sp(); pd(abs(%s)); nl();' % c_int(a if a != -32768 else 5)]
        a, b = self.i32(), self.i32() or 3
        if a == -2**31 and b == -1:
            b = 7
        return ['ldiv_t q = ldiv(%s, %s);' % (c_long(a), c_long(b)), 'tag(%d, "ldiv");' % tid,
                'px((uint32_t)q.quot); sp(); px((uint32_t)q.rem); sp(); px((uint32_t)labs(%s)); nl();'
                % c_long(a if a != -2**31 else 5)]

    # ---------------------------------------------------------- strings

    def t_string(self, tid):
        a, b = self.word(), self.word()
        op = self.pick(('strlen', 'strcmp', 'strncmp', 'memcmp', 'stricmp', 'strchr', 'strrchr',
                        'strstr', 'strspn', 'strcspn', 'strpbrk', 'memchr', 'strcoll'))
        pre = ['static const char a[] = %s;' % c_str(a), 'static const char b[] = %s;' % c_str(b)]
        c = ord(self.pick(ALPHA + '\0q')) if self.r.random() < 0.9 else 0
        if op == 'strlen':
            expr = 'pd((int32_t)strlen(a));'
        elif op in ('strcmp', 'stricmp', 'strcoll'):
            expr = 'pd(sgn(%s(a, b)));' % op
        elif op == 'strncmp':
            expr = 'pd(sgn(strncmp(a, b, %d)));' % self.r.randint(0, 14)
        elif op == 'memcmp':
            expr = 'pd(sgn(memcmp(a, b, %d)));' % self.r.randint(0, min(len(a), len(b)) + 1)
        elif op in ('strchr', 'strrchr'):
            expr = 'pidx(a, %s(a, %d));' % (op, c)
        elif op == 'strstr':
            expr = 'pidx(a, strstr(a, b));'
        elif op in ('strspn', 'strcspn'):
            expr = 'pd((int32_t)%s(a, b));' % op
        elif op == 'strpbrk':
            expr = 'pidx(a, strpbrk(a, b));'
        else:
            expr = 'pidx(a, memchr(a, %d, %d));' % (c, self.r.randint(0, len(a) + 1))
        return pre + ['tag(%d, "%s");' % (tid, op), expr, 'nl();']

    def t_copy(self, tid):
        a = self.word(10)
        op = self.pick(('strcpy', 'strcat', 'strncpy', 'strncat', 'memmove', 'memcpy', 'memset',
                        'strdup', 'strxfrm'))
        init = self.word(8)
        lines = ['char d[40];', 'char *r;', "memset(d, '#', sizeof d);",
                 'strcpy(d, %s);' % c_str(init)]
        if op == 'strcpy':
            lines += ['r = strcpy(d + %d, %s);' % (self.r.randint(0, 9), c_str(a))]
        elif op == 'strcat':
            lines += ['r = strcat(d, %s);' % c_str(a)]
        elif op == 'strncpy':
            lines += ['r = strncpy(d + %d, %s, %d);' % (self.r.randint(0, 9), c_str(a), self.r.randint(0, 14))]
        elif op == 'strncat':
            lines += ['r = strncat(d, %s, %d);' % (c_str(a), self.r.randint(0, 14))]
        elif op in ('memmove', 'memcpy'):
            n = self.r.randint(0, 16)
            i, j = self.r.randint(0, 20), self.r.randint(0, 20)
            if op == 'memcpy' and abs(i - j) < n:
                op = 'memmove'           # overlapping memcpy is undefined
            lines = ['char d[40];', 'char *r;', 'int k;', "for (k = 0; k < 40; k++) d[k] = (char)('A' + k);",
                     'r = %s(d + %d, d + %d, %d);' % (op, i, j, n)]
        elif op == 'memset':
            lines += ['r = memset(d + %d, %d, %d);' % (self.r.randint(0, 9), self.r.randint(0, 255),
                                                      self.r.randint(0, 20))]
        elif op == 'strdup':
            return ['char *p = strdup(%s);' % c_str(a), 'tag(%d, "strdup");' % tid,
                    'pstr(p); nl();', 'free(p);']
        else:
            lines += ['pd((int32_t)strxfrm(d, %s, 39)); sp();' % c_str(a), 'r = d;']
        return lines + ['tag(%d, "%s");' % (tid, op), 'pd((int32_t)(r - d)); sp(); pmem(d, 32); nl();']

    def t_strtok(self, tid):
        s = ''.join(self.pick('ab ,;') for _ in range(self.r.randint(0, 16)))
        delims = [self.pick((' ', ',', ' ,', ';,', 'a', '')) for _ in range(4)]
        lines = ['char buf[24];', 'char *t;', 'strcpy(buf, %s);' % c_str(s),
                 'tag(%d, "strtok");' % tid, 't = strtok(buf, %s);' % c_str(delims[0])]
        for d in delims[1:]:
            lines += ['pidx(buf, t); sp(); if (t) pstr(t); sp();',
                      'if (t) t = strtok(NULL, %s);' % c_str(d)]
        return lines + ['pidx(buf, t); nl();']

    def t_ctype(self, tid):
        cs = [self.r.randint(0, 255) for _ in range(6)] + [self.pick((-1, 0, 32, 127, 128, 255))]
        lines = ['static const int cs[] = { %s };' % ', '.join(map(str, cs)), 'int k;',
                 'tag(%d, "ctype");' % tid, 'for (k = 0; k < %d; k++) {' % len(cs),
                 '    int c = cs[k];',
                 '    pd(!!isalpha(c) | !!isdigit(c) << 1 | !!isalnum(c) << 2 | !!isspace(c) << 3 |',
                 '       !!isupper(c) << 4 | !!islower(c) << 5 | !!isxdigit(c) << 6 | !!isprint(c) << 7 |',
                 '       !!isgraph(c) << 8 | !!iscntrl(c) << 9 | !!ispunct(c) << 10);',
                 "    putchar(','); pd(toupper(c)); putchar(','); pd(tolower(c)); sp();", '}', 'nl();']
        return lines

    # ---------------------------------------------------------- sorting

    def t_qsort(self, tid):
        kind = self.pick(('int', 'int', 'str', 'rec3', 'krec', 'bsearch'))
        n = self.r.randint(0, 20)
        if kind == 'int':
            vals = [self.r.randint(-50, 50) if self.r.random() < 0.5 else self.i16() for _ in range(n)]
            return ['int a[%d] = { %s };' % (max(n, 1), ', '.join(map(c_int, vals)) or '0'), 'int k;',
                    'qsort(a, %d, sizeof a[0], cmp_int);' % n, 'tag(%d, "qsort int");' % tid,
                    'for (k = 0; k < %d; k++) { pd(a[k]); sp(); }' % n, 'nl();']
        if kind == 'str':
            vals = [self.word(5) for _ in range(n)]
            return ['const char *a[%d] = { %s };' % (max(n, 1), ', '.join(map(c_str, vals)) or '0'),
                    'int k;', 'qsort(a, %d, sizeof a[0], cmp_str);' % n, 'tag(%d, "qsort str");' % tid,
                    'for (k = 0; k < %d; k++) { pstr(a[k]); sp(); }' % n, 'nl();']
        if kind == 'rec3':
            data = ''.join(self.pick('abc') for _ in range(3 * n))
            return ['char a[%d];' % (3 * n + 1), 'memcpy(a, %s, %d);' % (c_str(data), 3 * n),
                    'qsort(a, %d, 3, cmp_rec3);' % n, 'tag(%d, "qsort rec3");' % tid,
                    'pmem(a, %d); nl();' % (3 * n)]
        if kind == 'krec':
            keys = [self.r.randint(-5, 5) for _ in range(n)]
            return ['struct krec a[%d] = { %s };' % (max(n, 1), ', '.join(
                        '{ %d, %d }' % (k, i) for i, k in enumerate(keys)) or '{ 0, 0 }'),
                    'int k;', 'qsort(a, %d, sizeof a[0], cmp_krec);' % n,
                    'tag(%d, "qsort krec");' % tid,
                    'for (k = 0; k < %d; k++) { pd(a[k].key); sp(); }' % n, 'nl();']
        vals = sorted(set(self.r.randint(-100, 100) for _ in range(n)))
        keys = [self.r.randint(-101, 101) for _ in range(5)] + vals[:2]
        lines = ['static const int a[%d] = { %s };' % (max(len(vals), 1), ', '.join(map(str, vals)) or '0'),
                 'int key;', 'const int *r;', 'tag(%d, "bsearch");' % tid]
        for k in keys:
            lines.append('key = %d; r = bsearch(&key, a, %d, sizeof a[0], cmp_int); '
                         'pd(r ? (int32_t)(r - a) : -1); sp();' % (k, len(vals)))
        return lines + ['nl();']

    # ---------------------------------------------------------- math

    def t_math_exact(self, tid):
        fn = self.pick(('fabsf', 'floorf', 'ceilf', 'fmodf', 'frexpf', 'ldexpf', 'modff',
                        'nextafterf', 'sqrtf', 'class'))
        x = self.real(-1e7, 1e7)
        if fn == 'fmodf':
            y = self.real(-1e4, 1e4) or 3.0
            call = 'pf(fmodf(x, %s));' % c_float(y)
        elif fn == 'frexpf':
            call = '{ int e; float m = frexpf(x, &e); pf(m); sp(); pd(e); }'
        elif fn == 'ldexpf':
            call = 'pf(ldexpf(x, %d));' % self.r.randint(-12, 12)
        elif fn == 'modff':
            call = '{ float ip; float fr = modff(x, &ip); pf(fr); sp(); pf(ip); }'
        elif fn == 'nextafterf':
            call = 'pf(nextafterf(x, %s));' % c_float(self.pick((0.0, 1e30, -1e30, x)))
        elif fn == 'sqrtf':
            x = abs(x)
            return ['volatile float vx = %s;' % c_float(x), 'float x = vx;',
                    'tag(%d, "sqrtf");' % tid, 'pf(sqrtf(x)); nl();']
        elif fn == 'class':
            x = self.pick((x, float('inf'), float('-inf'), float('nan'), -0.0))
            call = 'pd(!!isnan(x) | !!isinf(x) << 1 | !!isfinite(x) << 2 | !!signbit(x) << 3);'
        else:
            call = 'pf(%s(x));' % fn
        return ['volatile float vx = %s;' % c_float(x), 'float x = vx;',
                'tag(%d, "%s");' % (tid, fn), call, 'nl();']

    def t_math_approx(self, tid):
        fn = self.pick(('sinf', 'cosf', 'tanf', 'asinf', 'acosf', 'atanf', 'atan2f', 'sinhf',
                        'coshf', 'tanhf', 'expf', 'logf', 'log10f', 'powf'))
        if fn in ('sinf', 'cosf', 'tanf'):
            x = self.real(-100, 100)
        elif fn in ('asinf', 'acosf'):
            x = f32(self.r.uniform(-1, 1))
        elif fn in ('sinhf', 'coshf', 'tanhf'):
            x = self.real(-10, 10)
        elif fn == 'expf':
            x = self.real(-20, 20)
        elif fn in ('logf', 'log10f'):
            x = abs(self.real(-1e6, 1e6)) or 1.0
        else:
            x = self.real(-1e3, 1e3)
        if fn == 'atan2f':
            call = '%s(x, %s)' % (fn, c_float(self.real(-1e3, 1e3)))
        elif fn == 'powf':
            if x < 0:
                y = float(self.r.randint(-5, 5))
            else:
                x = x or 2.0
                y = f32(self.r.uniform(-4, 4))
            if abs(x) > 1e2:
                y = f32(max(-2.0, min(2.0, y)))
            call = 'powf(x, %s)' % c_float(y)
        else:
            call = '%s(x)' % fn
        if fn == 'tanf':
            # Reduction by a float pi shifts the argument by up to 3e-6 near
            # 100, which tan magnifies near its poles.
            x = f32(max(-10.0, min(10.0, x)))
            if abs(math.cos(x)) < 1e-2:
                x = f32(x + 0.1)
        return ['volatile float vx = %s;' % c_float(x), 'float x = vx;',
                'tag(%d, "~%s");' % (tid, fn), 'pf(%s); nl();' % call]

    # ---------------------------------------------------------- heap

    def t_heap(self, tid):
        lines = ['char *p[6] = { 0, 0, 0, 0, 0, 0 };', 'int z;', 'tag(%d, "heap");' % tid]
        sizes = [0] * 6
        for step in range(self.r.randint(3, 12)):
            k = self.r.randrange(6)
            seed = self.r.randint(0, 255)
            op = self.pick(('malloc', 'malloc', 'calloc', 'realloc', 'free'))
            n = self.r.randint(1, 300)
            if op == 'malloc':
                if sizes[k]:
                    lines.append('free(p[%d]);' % k)
                lines += ['p[%d] = malloc(%d); pd(p[%d] != 0);' % (k, n, k),
                          'if (p[%d]) fill(p[%d], %d, %d);' % (k, k, n, seed)]
                sizes[k] = (n, seed)
            elif op == 'calloc':
                if sizes[k]:
                    lines.append('free(p[%d]);' % k)
                m = self.r.randint(1, 8)
                lines += ['p[%d] = calloc(%d, %d); pd(p[%d] != 0);' % (k, m, (n + m - 1) // m, k),
                          'for (z = 0; p[%d] && z < %d; z++) if (p[%d][z]) break;' % (k, m * ((n + m - 1) // m), k),
                          'pd(z); if (p[%d]) fill(p[%d], %d, %d);' % (k, k, n, seed)]
                sizes[k] = (n, seed)
            elif op == 'realloc' and sizes[k]:
                old, oseed = sizes[k]
                lines += ['p[%d] = realloc(p[%d], %d); pd(p[%d] != 0);' % (k, k, n, k),
                          'if (p[%d]) { pd(check(p[%d], %d, %d)); fill(p[%d], %d, %d); }'
                          % (k, k, min(old, n), oseed, k, n, seed)]
                sizes[k] = (n, seed)
            elif op == 'free' and sizes[k]:
                n, oseed = sizes[k]
                lines += ['pd(check(p[%d], %d, %d)); free(p[%d]); p[%d] = 0;' % (k, n, oseed, k, k)]
                sizes[k] = 0
            lines.append("putchar(',');")
        for k in range(6):
            if sizes[k]:
                n, oseed = sizes[k]
                lines.append('if (p[%d]) { pd(check(p[%d], %d, %d)); free(p[%d]); }' % (k, k, n, oseed, k))
        return lines + ['nl();']

    # ---------------------------------------------------------- va_list

    def t_vprintf(self, tid):
        fmt, args = '', []
        for _ in range(self.r.randint(1, 3)):
            if self.r.random() < 0.5:
                fmt += ''.join(self.pick('ab =:,') for _ in range(self.r.randint(1, 3)))
            text, arg, _ = self.printf_conv()
            fmt += text
            if arg is not None:
                args.append(arg)
        rest = ''.join(', ' + a for a in args)
        sink = self.pick(('vsprintf', 'vsnprintf', 'printf', 'vprintf', 'fprintf', 'vfprintf', 'puts'))
        if sink == 'vsprintf':
            return ['char b[100];', 'int r;', 'r = vsp(b, %s%s);' % (c_str(fmt), rest),
                    'tag(%d, "vsprintf");' % tid, 'pd(r); sp(); pstr(b); nl();']
        if sink == 'vsnprintf':
            size = self.r.randint(0, 30)
            return ['char b[100];', 'int r;', "memset(b, '#', sizeof b);",
                    'r = vsnp(b, %d, %s%s);' % (size, c_str(fmt), rest),
                    'tag(%d, "vsnprintf %d");' % (tid, size), 'pd(r); sp(); pmem(b, %d); nl();' % (size + 2)]
        if sink == 'puts':
            return ['int r;', 'tag(%d, "puts");' % tid, 'r = puts(%s);' % c_str(self.word()),
                    'pd(r >= 0); nl();']
        call = {'printf': 'printf(%s%s)', 'vprintf': 'vp(%s%s)',
                'fprintf': 'fprintf(stdout, %s%s)', 'vfprintf': 'vfp(stdout, %s%s)'}[sink]
        return ['int r;', 'tag(%d, "%s");' % (tid, sink), 'r = ' + call % (c_str(fmt), rest) + ';',
                'ps(" ="); pd(r); nl();']

    # ---------------------------------------------------------- files

    FILE_DATA = 'abcxyz0123 ,\n'

    def file_bytes(self, n):
        return ''.join(self.pick(self.FILE_DATA) for _ in range(n))

    def t_file(self, tid):
        name, name2 = 'F%d.TMP' % tid, 'G%d.TMP' % tid
        out = ['FILE *f;', 'char b[700];', 'int r;', 'long p;', 'fpos_t pos;', 'size_t n;',
               'remove("%s"); remove("%s");' % (name, name2), 'tag(%d, "file");' % tid]
        if self.r.random() < 0.15:
            data = self.file_bytes(self.r.randint(0, 120))
            return out + ['f = tmpfile(); pd(f != 0); sp();', 'if (!f) { nl(); return; }',
                          'pd((int32_t)fwrite(%s, 1, %d, f)); sp();' % (c_str(data), len(data)),
                          'rewind(f);', 'n = fread(b, 1, %d, f); pd((int32_t)n); sp(); pmem(b, (int)n); sp();'
                          % len(data), 'pd(fclose(f)); nl();']
        # Create, keeping a model of the contents.
        content = ''
        wmode = self.pick(('w', 'wb', 'w+', 'wb+'))
        out += ['f = fopen("%s", "%s"); pd(f != 0); sp();' % (name, wmode), 'if (!f) { nl(); return; }']
        for _ in range(self.r.randint(1, 5)):
            kind = self.pick(('fputc', 'fputs', 'fwrite', 'fprintf'))
            if kind == 'fputc':
                c = self.pick(self.FILE_DATA)
                out.append('pd(fputc(%d, f)); sp();' % ord(c))
                content += c
            elif kind == 'fputs':
                t = self.file_bytes(self.r.randint(0, 30))
                out.append('pd(fputs(%s, f) >= 0); sp();' % c_str(t))
                content += t
            elif kind == 'fwrite':
                t = self.file_bytes(self.r.randint(0, 40))
                out.append('pd((int32_t)fwrite(%s, 1, %d, f)); sp();' % (c_str(t), len(t)))
                content += t
            else:
                v, w = self.r.randint(-999, 999), self.word(6).replace('\t', ' ')
                t = '%d,%s;' % (v, w)
                out.append('pd(fprintf(f, "%%d,%%s;", %d, %s)); sp();' % (v, c_str(w)))
                content += t
        if self.r.random() < 0.3:
            pad = (-len(content)) % 128 or 128
            out.append('for (r = 0; r < %d; r++) fputc(%d, f);' % (pad, ord('p')))
            content += 'p' * pad
        out.append('pd(fclose(f)); sp();')
        aligned = len(content) % 128 == 0
        # Reopen and wander.
        mode = self.pick(('r', 'rb', 'r+', 'rb+', 'a', 'a+'))
        out += ['f = fopen("%s", "%s"); pd(f != 0); sp();' % (name, mode), 'if (!f) { nl(); return; }']
        # Where an append stream starts, for reading and for ftell, is
        # implementation-defined: wait for a write or seek to fix it.
        pos = None if mode.startswith('a') else 0
        can_read = mode != 'a'
        can_write = mode != 'r' and mode != 'rb'
        last = None                      # 'r' or 'w': a switch needs a seek
        eof = False
        saved = None
        unget = False                    # glibc's SEEK_CUR goes wrong after a pushback
        for _ in range(self.r.randint(2, 12)):
            n = len(content)
            ops = ['seek', 'rewind', 'status'] + (['ftell'] if pos is not None else [])
            if can_read and pos is not None:
                ops += ['fgetc', 'fread', 'fgets', 'fread', 'ungetc']
            if can_write:
                ops += ['write', 'write']
            if pos is not None:
                ops += ['getpos'] if saved is None else ['setpos']
            op = self.pick(ops)
            room = n - (pos or 0)        # readable bytes before the logical end
            if op in ('fgetc', 'fread', 'fgets', 'ungetc'):
                if last == 'w':
                    out.append('fseek(f, 0L, SEEK_CUR);')
                last = 'r'
            if op == 'fgetc':
                if room <= 0 and not aligned:
                    continue
                out.append('pd(fgetc(f)); sp();')
                if room > 0:
                    pos += 1
                else:
                    eof = True
            elif op == 'fread':
                k = self.r.randint(0, 40)
                if not aligned:
                    k = min(k, room)
                out.append('n = fread(b, 1, %d, f); pd((int32_t)n); sp(); pmem(b, (int)n); sp();' % k)
                got = max(0, min(k, room))
                pos += got
                if got < k:
                    eof = True
            elif op == 'fgets':
                k = self.r.randint(2, 40)
                if not aligned:
                    k = min(k, room + 1)
                if k < 2:
                    continue
                out.append('pd(fgets(b, %d, f) != 0); sp(); pstr(b); sp();' % k)
                chunk = content[pos:pos + k - 1]
                if '\n' in chunk:
                    chunk = chunk[:chunk.index('\n') + 1]
                if not chunk:
                    eof = True
                    out[-1] = 'pd(fgets(b, %d, f) != 0); sp();' % k
                elif len(chunk) < k - 1 and pos + len(chunk) >= n:
                    eof = True
                pos += len(chunk)
            elif op == 'ungetc':
                # At position 0 a pushback leaves the position indeterminate.
                if room <= 0 or not pos:
                    continue
                c = self.pick('QRS')
                # A pushback is read straight back, so the position is unchanged.
                out.append('pd(ungetc(%d, f)); sp(); pd(fgetc(f)); sp();' % ord(c))
                eof = False
                unget = True
            elif op == 'write':
                if last == 'r':
                    out.append('fseek(f, %dL, SEEK_SET);' % pos if unget else 'fseek(f, 0L, SEEK_CUR);')
                    unget = False
                last = 'w'
                t = self.file_bytes(self.r.randint(1, 30))
                if mode.startswith('a'):
                    pos = n
                if self.r.random() < 0.5:
                    out.append('pd((int32_t)fwrite(%s, 1, %d, f)); sp();' % (c_str(t), len(t)))
                else:
                    out.append('pd(fputs(%s, f) >= 0); sp();' % c_str(t))
                content = content[:pos] + t + content[pos + len(t):]
                pos += len(t)
                aligned = len(content) % 128 == 0
                eof = False
            elif op == 'ftell':
                out.append('pd(ftell(f)); sp();')
            elif op == 'seek':
                how = self.pick(('SEEK_SET', 'SEEK_CUR', 'SEEK_END') if aligned else ('SEEK_SET', 'SEEK_CUR'))
                if how == 'SEEK_SET':
                    target = self.r.randint(0, n)
                    off = target
                elif how == 'SEEK_CUR' and pos is not None and not unget:
                    target = self.r.randint(0, n)
                    off = target - pos
                else:
                    how = 'SEEK_END' if aligned else 'SEEK_SET'
                    target = self.r.randint(0, n)
                    off = target - n if aligned else target
                out.append('pd(fseek(f, %dL, %s) != 0); sp();' % (off, how))
                pos, eof, last, unget = target, False, None, False
            elif op == 'rewind':
                out.append('rewind(f);')
                pos, eof, last = 0, False, None
            elif op == 'status':
                out.append('pd(!!feof(f)); pd(!!ferror(f)); sp();')
                if eof:
                    out.append('clearerr(f);')
                    eof = False
            elif op == 'getpos':
                out.append('pd(fgetpos(f, &pos)); sp();')
                saved = pos
            else:
                out.append('pd(fsetpos(f, &pos)); sp();')
                pos, eof, last = saved, False, None
                saved = None
        out.append('pd(fclose(f)); sp();')
        # Verify the contents, then rename and remove.
        n = len(content)
        out += ['f = fopen("%s", "rb");' % name,
                'if (f) { n = fread(b, 1, %d, f); pd((int32_t)n); sp(); pmem(b, (int)n); sp(); fclose(f); }' % n]
        if self.r.random() < 0.5:
            out += ['pd(rename("%s", "%s") == 0); sp();' % (name, name2),
                    'f = fopen("%s", "r"); pd(f != 0); sp(); if (f) fclose(f);' % name,
                    'pd(remove("%s") == 0); sp();' % name2]
        else:
            out.append('pd(remove("%s") == 0); sp();' % name)
        out.append('f = fopen("%s", "r"); pd(f != 0); if (f) fclose(f); nl();' % name)
        return out

    # ---------------------------------------------------------- time

    TIME_CONV = 'aAbBCcdHIjmMpSUwWxXyY%'

    def time_value(self):
        return self.pick((0, 1, 59, 86399, 86400, 951782400, 946684799, 1234567890, 2147483647,
                          68169599, 1709251199, self.r.randint(0, 2**31 - 1), self.r.randint(0, 2**31 - 1)))

    def t_time(self, tid):
        t = self.time_value()
        fmt = ''
        for _ in range(self.r.randint(1, 4)):
            if self.r.random() < 0.4:
                fmt += self.pick((' ', '-', ':', '/', 'x', '[]'))
            fmt += '%' + self.pick(self.TIME_CONV)
        size = 80 if self.r.random() < 0.8 else self.r.randint(0, 12)
        d = self.r.randint(-10**6, 10**6)
        if not -2**31 <= t - d < 2**31:
            d = -d                       # t - d must not overflow time_t
        return ['time_t t = %dL;' % t, 'struct tm c;', 'char b[90];', 'size_t r;',
                'c = *gmtime(&t);', 'tag(%d, "time");' % tid, 'ptm(&c);',
                'r = strftime(b, %d, %s, &c); pd((int32_t)r); sp(); if (r) pstr(b); sp();' % (size, c_str(fmt)),
                'pstr(asctime(&c));',
                'px((uint32_t)mktime(&c)); sp();',
                'pd((int32_t)difftime(t, t - %dL));' % d, 'nl();']

    def t_mktime(self, tid):
        f = dict(year=self.r.randint(75, 132), mon=self.r.randint(-15, 26), mday=self.r.randint(-40, 70),
                 hour=self.r.randint(-30, 60), min=self.r.randint(-100, 200), sec=self.r.randint(-100, 200))
        if self.r.random() < 0.4:
            f.update(mon=self.r.randint(0, 11), mday=self.r.randint(1, 28), hour=self.r.randint(0, 23),
                     min=self.r.randint(0, 59), sec=self.r.randint(0, 59))
        return ['struct tm c;', 'time_t t;', 'memset(&c, 0, sizeof c);',
                'c.tm_year = %(year)d; c.tm_mon = %(mon)d; c.tm_mday = %(mday)d;' % f,
                'c.tm_hour = %(hour)d; c.tm_min = %(min)d; c.tm_sec = %(sec)d;' % f,
                't = mktime(&c);', 'tag(%d, "mktime");' % tid, 'px((uint32_t)t); sp(); ptm(&c); nl();']

    # ---------------------------------------------------------- properties

    def t_rand(self, tid):
        seed = self.pick((0, 1, 2, 12345, 32767, 65535, self.r.randint(0, 65535)))
        return ['int a[8];', 'int k;', 'int same = 1;', 'int inrange = 1;', 'srand(%du);' % seed,
                'for (k = 0; k < 8; k++) { a[k] = rand(); if (a[k] < 0 || a[k] > RAND_MAX) inrange = 0; }',
                'srand(%du);' % seed, 'for (k = 0; k < 8; k++) if (rand() != a[k]) same = 0;',
                'tag(%d, "rand");' % tid, 'pd(same); sp(); pd(inrange); sp(); pd(RAND_MAX >= 32767); nl();']

    def t_setjmp(self, tid):
        depth, val = self.r.randint(0, 6), self.pick((0, 1, 2, -1, 255, 32767))
        return ['volatile int n = 0;', 'int r;', 'r = setjmp(jb);',
                'if (r == 0) { n = %d; jdeep(%d, %d); n = -99; }' % (depth + 10, depth, val),
                'tag(%d, "setjmp");' % tid, 'pd(r); sp(); pd(n); nl();']

    def t_mb(self, tid):
        w = self.word(10)
        return ['static const char s[] = %s;' % c_str(w), 'wchar_t w[16];', 'char c[16];',
                'wchar_t wc = 0;', 'size_t n;', 'tag(%d, "mb");' % tid,
                'n = mbstowcs(w, s, 16); pd((int32_t)n); sp();',
                "memset(c, '#', sizeof c); n = wcstombs(c, w, 16); pd((int32_t)n); sp(); pstr(c); sp();",
                'pd(mblen(s, %d)); sp(); pd(mbtowc(&wc, s, %d)); sp(); pd((int32_t)wc); sp();' % (len(w) + 1, len(w) + 1),
                'pd(wctomb(c, %d)); sp(); pd(c[0]); nl();' % ord(self.pick('AZaz09 '))]

    TESTS = (('t_sprintf', 8), ('t_sscanf', 5), ('t_strtol', 4), ('t_atoi', 1), ('t_strtod', 2),
             ('t_div', 1), ('t_string', 5), ('t_copy', 4), ('t_strtok', 1), ('t_ctype', 1),
             ('t_qsort', 2), ('t_math_exact', 4), ('t_math_approx', 4), ('t_heap', 1),
             ('t_vprintf', 3), ('t_file', 4), ('t_time', 3), ('t_mktime', 2), ('t_rand', 1),
             ('t_setjmp', 1), ('t_mb', 1))

    def program(self, count, only=None):
        names = [n for n, w in self.TESTS for _ in range(w)]
        body, calls = [], []
        for tid in range(1, count + 1):
            lines = getattr(self, self.pick(names))(tid)
            if only is not None and tid not in only:
                continue
            body.append('static void t%d(void)\n{\n%s\n}\n' % (tid, '\n'.join('    ' + ln for ln in lines)))
            calls.append('    t%d();' % tid)
        calls = ['    atexit(atexit1);', '    atexit(atexit2);'] + calls
        return PRELUDE + '\n' + '\n'.join(body) + '\nint main(void)\n{\n' + '\n'.join(calls) + \
            '\n    return 0;\n}\n'


# ------------------------------------------------------------------ driver

def host_expected(source, work, timeout):
    exe = work / 'host'
    r = subprocess.run(['clang', *HOST_FLAGS, str(source), '-o', str(exe), '-lm'],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return None, 'host build failed: ' + (r.stderr.strip().splitlines() or ['?'])[0]
    files = work / 'hostfiles'
    files.mkdir(exist_ok=True)
    env = dict(os.environ, TZ='UTC0')    # dcc has no time zone
    try:
        run = subprocess.run([str(exe)], capture_output=True, timeout=timeout, stdin=subprocess.DEVNULL,
                             cwd=files, env=env)
    except subprocess.TimeoutExpired:
        return None, 'host run timed out'
    if run.returncode != 0:
        return None, 'host run failed with %d' % run.returncode
    return run.stdout.decode('latin-1'), ''


def dcc_output(source, build, peep, args):
    shutil.rmtree(build, ignore_errors=True)
    cmd = [str(REPO / 'dccmake'), 'dcc-input=%s' % source, 'dcc-output=T',
           'dcc-build-dir=%s' % build, 'dcc-peep=%s' % ('true' if peep else 'false'),
           'dcc-stack-bytes=2048', 'dcc-floatio=true', 'dcc-flongio=true',
           'ntvcm-tool=%s' % args.emulator]
    try:
        r = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, stdin=subprocess.DEVNULL,
                           timeout=args.build_timeout)
    except subprocess.TimeoutExpired:
        return None, 'build timed out'
    if r.returncode != 0:
        log = [ln for ln in (r.stdout + r.stderr).splitlines() if not ln.startswith('+')]
        errors = [ln for ln in log if 'error' in ln.lower() or 'fatal' in ln.lower()]
        return None, 'build failed: ' + ((errors or log or ['?'])[0].replace(str(source) + ':', ''))[:160]
    try:
        run = subprocess.run([args.emulator, '-s:0', 'T.COM'], cwd=build, capture_output=True,
                             timeout=args.timeout, stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        return None, 'run timed out'
    return run.stdout.decode('latin-1').replace('\r', ''), ''


def close_enough(got, want):
    """Transcendental results: the documented 5-6 significant digits."""
    try:
        g = struct.unpack('<f', struct.pack('<I', int(got, 16)))[0]
        w = struct.unpack('<f', struct.pack('<I', int(want, 16)))[0]
    except ValueError:
        return False
    if math.isnan(w) or math.isnan(g):
        return math.isnan(w) and math.isnan(g)
    if math.isinf(w) or math.isinf(g):
        return g == w or (math.isinf(w) and abs(g) > 1e37)
    return abs(g - w) <= 3e-5 * max(abs(w), 1.0)


def within_ulp(got, want, ulp):
    """A ^ line: its first field, float bits, within ulp; the rest exact."""
    g, w = got.split(' ', 1) + [''], want.split(' ', 1) + ['']
    try:
        gi, wi = int(g[0], 16), int(w[0], 16)
    except ValueError:
        return False
    if (gi ^ wi) & 0x80000000:
        return False
    return abs(gi - wi) <= ulp and g[1] == w[1]


def compare(got, want, ulp=0):
    """Mismatching test lines as (id, name, got, want)."""
    bad = []
    glines, wlines = got.splitlines(), want.splitlines()
    for i, w in enumerate(wlines):
        g = glines[i] if i < len(glines) else '<missing>'
        if g == w:
            continue
        m = re.match(r'(\d+) (~?)([^:]*): (.*)$', w)
        if m and m.group(2):
            gm = re.match(r'(\d+) ~[^:]*: (.*)$', g)
            if gm and close_enough(gm.group(2), m.group(4)):
                continue
        if m and m.group(3).startswith('^') and ulp:
            gm = re.match(r'(\d+) \^[^:]*: (.*)$', g)
            if gm and within_ulp(gm.group(2), m.group(4), ulp):
                continue
        bad.append((m.group(1) if m else '?', (m.group(3) if m else w).split()[0].lstrip('^'), g, w))
    if len(glines) > len(wlines):
        bad.append(('?', 'extra output', glines[len(wlines)], ''))
    return bad


def run_seed(seed, args):
    work = Path(args.work) / str(seed)
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    source = work / 't.c'
    source.write_text(Gen(seed).program(args.tests), encoding='latin-1')
    want, why = host_expected(source, work, args.timeout)
    if want is None:
        return seed, 'INVALID', why, []
    results = []
    for peep in (False, True):
        got, why = dcc_output(source, work / ('peep' if peep else 'nopeep'), peep, args)
        if got is None:
            return seed, 'BUILD', why, []
        results.append(('peep' if peep else 'nopeep', compare(got, want, args.ulp)))
    bad = [(cfg, b) for cfg, rows in results for b in rows]
    if not bad:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
        return seed, 'ok', '', []
    return seed, 'FAIL', '', bad


def parse_ids(text):
    out = set()
    for part in text.split(','):
        lo, _, hi = part.partition('-')
        out.update(range(int(lo), int(hi or lo) + 1))
    return out


def fuzz(args):
    seeds = sorted(parse_ids(args.seeds))
    counts = {}
    names = {}
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for seed, kind, why, bad in pool.map(lambda s: run_seed(s, args), seeds):
            counts[kind] = counts.get(kind, 0) + 1
            if kind in ('INVALID', 'BUILD'):
                print('seed %d: %s %s' % (seed, kind, why), flush=True)
            for cfg, (tid, name, got, want) in bad:
                first = name not in names
                names.setdefault(name, []).append((seed, tid))
                if first or args.verbose:
                    print('seed %d test %s %s [%s]\n  dcc:  %s\n  host: %s' % (
                        seed, tid, name, cfg, got[:200], want[:200]), flush=True)
    print('\n%d programs: %s' % (len(seeds), ', '.join('%s %d' % (k, counts[k]) for k in sorted(counts))))
    for name, where in sorted(names.items()):
        print('  %-12s %4d mismatches, first seed %d test %s' % (name, len(where), where[0][0], where[0][1]))
    return 1 if names or counts.get('BUILD') else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--seeds', default='1-50')
    parser.add_argument('--tests', type=int, default=40, help='tests per program')
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    parser.add_argument('--work', default=str(REPO / 'build' / 'rtl-oracle-fuzz'))
    parser.add_argument('--emulator', default='ntvcm')
    parser.add_argument('--timeout', type=float, default=30)
    parser.add_argument('--build-timeout', type=float, default=300)
    parser.add_argument('--ulp', type=int, default=0,
                        help='accept strtod results this many float ulps away')
    parser.add_argument('--keep', action='store_true', help='keep passing seeds too')
    parser.add_argument('--verbose', action='store_true', help='print every mismatch')
    parser.add_argument('--generate', nargs=2, metavar=('SEED', 'C'))
    parser.add_argument('--only', help='with --generate, keep only these test ids')
    args = parser.parse_args()
    if args.generate:
        only = parse_ids(args.only) if args.only else None
        Path(args.generate[1]).write_text(Gen(int(args.generate[0])).program(args.tests, only),
                                          encoding='latin-1')
        return 0
    return fuzz(args)


if __name__ == '__main__':
    sys.exit(main())
