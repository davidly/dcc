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
                    the documented 5-6 significant digits; arithmetic rounding,
                    overflow, and the target subnormal policy
  malloc family     allocate, grow, shrink, free, and verify live blocks
                    survive allocation churn and failed realloc; forced coalescing
  div/ldiv/abs      quotients and remainders
  va_list printf    vsprintf, vsnprintf, vprintf, vfprintf, and printf,
                    fprintf and puts to stdout
  file I/O          create, reopen in every mode, then random reads, writes,
                    seeks, pushback and status checks against a model of the
                    file; verify, rename and remove; tmpfile; descriptor
                    writes across CP/M record and extent boundaries; append
                    visibility after flush; text and binary Ctrl-Z reads; stream
                    flag transitions; descriptor exhaustion and recovery
  time.h            gmtime, asctime, strftime, difftime, mktime round trips
                    and normalization
  properties        rand repeatability and range, setjmp/longjmp from nested
                    calls, atexit order, multibyte round trips

File tests stay inside each file's logical length, or use lengths that are a
multiple of 128: CP/M pads the last record with Ctrl-Z, which a read past the
logical end returns and SEEK_END on a reopened file counts. The host runs in
UTC, as dcc has no time zone.

The program is compiled by dcc and run under the emulator, and compiled by
native Clang and run on the host. The host oracle models the target's 32-bit
long and calendar time ranges where they affect results. Tests print through
putchar only, so a wrong printf cannot hide another result, and they are
independent: a mismatching line names the failing call. Results that depend
on dcc's 16-bit int stay in range on both sides.

  rtl-oracle-fuzz.py --seeds 1-200
  rtl-oracle-fuzz.py --io-matrix                exercise all four float/long builds
  rtl-oracle-fuzz.py --rtl-coverage             record executed DCCRTL assembly labels
  rtl-oracle-fuzz.py --generate 17 prog.c          only write seed 17's program
  rtl-oracle-fuzz.py --generate 17 prog.c --only 5,9
"""
import argparse
import concurrent.futures
from contextlib import contextmanager
from datetime import datetime
from collections import Counter
import csv
import math
import os
from pathlib import Path
import random
import re
import shutil
import struct
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parent.parent


@contextmanager
def progress_terminal():
    if sys.stdout.isatty():
        yield sys.stdout
        return
    try:
        terminal = open('/dev/tty', 'w', encoding='utf-8')
    except OSError:
        yield None
    else:
        with terminal:
            yield terminal


def progress_reporter(total, terminal):
    started = time.monotonic()
    progress = ''

    def update(completed):
        nonlocal progress
        filled = 24 * completed // total
        finish = 'ETA --:--:--'
        if completed:
            now = time.time()
            remaining = (time.monotonic() - started) * (total - completed) / completed
            finish_at = datetime.fromtimestamp(now + remaining)
            clock_format = '%H:%M:%S'
            if finish_at.date() != datetime.fromtimestamp(now).date():
                clock_format = '%Y-%m-%d ' + clock_format
            label = 'Finished' if completed == total else 'ETA'
            finish = '%s %s' % (label, finish_at.strftime(clock_format))
        progress = '\r[%-24s] %d/%d programs  %s\033[K' % (
            '#' * filled, completed, total, finish)
        print(progress, end='', file=terminal, flush=True)

    def report(message):
        nonlocal progress
        if progress:
            print('\r\033[K', end='', file=terminal, flush=True)
            if terminal is not sys.stdout:
                progress = ''
        print(message, flush=True)
        if progress:
            print(progress, end='', file=terminal, flush=True)

    return update, report
HOST_FLAGS = ['-std=gnu99', '-w', '-fsigned-char', '-ffp-contract=off', '-fno-builtin']

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
#include <fcntl.h>
#include <unistd.h>
#ifdef _DCC_
#include <dirent.h>
#endif
#ifndef _DCC_
#include <limits.h>
#include <strings.h>
#include <sys/resource.h>
#define stricmp strcasecmp
/* dcc's strtod and atof produce float; round once, as it does. */
#define strtod strtof
#if ULONG_MAX > UINT32_MAX
/* Model CP/M's 32-bit long parsing when the host has 64-bit long. */
static long dcc_oracle_strtol(const char *s, char **end, int base)
{
    long v = strtol(s, end, base);
    if (v > INT32_MAX) { errno = ERANGE; return INT32_MAX; }
    if (v < INT32_MIN) { errno = ERANGE; return INT32_MIN; }
    return v;
}
static unsigned long dcc_oracle_strtoul(const char *s, char **end, int base)
{
    const char *p = s;
    char *parsed = (char *)s;
    unsigned long v = 0;
    int negative;

    while (isspace((unsigned char)*p)) ++p;
    negative = (*p == '-');
    if (negative) {
        /* Check the magnitude before applying the unsigned negation. */
        ++p;
        if (isalnum((unsigned char)*p)) {
            v = strtoul(p, &parsed, base);
            if (parsed == p) parsed = (char *)s;
        }
    } else {
        v = strtoul(s, &parsed, base);
    }
    if (end) *end = parsed;
    if (v > UINT32_MAX) { errno = ERANGE; return UINT32_MAX; }
    return negative ? (uint32_t)(0u - (uint32_t)v) : v;
}
#define strtol dcc_oracle_strtol
#define strtoul dcc_oracle_strtoul
#endif

/* CP/M rejects times outside 1970 through the signed 32-bit limit
 * and leaves the input fields unchanged on failure. */
static time_t dcc_oracle_mktime(struct tm *t)
{
    struct tm normalized = *t;
    time_t v = mktime(&normalized);
    if ((uintmax_t)v > INT32_MAX) return (time_t)-1;
    *t = normalized;
    return v;
}
#define mktime dcc_oracle_mktime
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

#ifdef _DCC_
/* Use the DCC heap snapshot to leave too little tail room for the request. */
static int heap_coalesce(int a_size, int b_size, int c_size, int order)
{
    meminfo_t initial, before, after;
    char *left, *a, *b, *c, *right, *tail = 0, *merged = 0;
    uintptr_t start;
    int need = a_size + b_size + c_size - 8;
    int result = 0;

    meminfo(&initial);
    left = malloc(37); a = malloc(a_size); b = malloc(b_size);
    c = malloc(c_size); right = malloc(53);
    if (!left || !a || !b || !c || !right) goto done;
    fill(left, 37, 11); fill(right, 53, 29);
    start = (uintptr_t)a;

    meminfo(&before);
    if (before.heap_growth > 48) {
        tail = malloc(before.heap_growth - 32);
        if (!tail) goto done;
        fill(tail, 16, 47);
    }
    meminfo(&before);
    if (before.heap_growth >= need) goto done;
    merged = malloc(need);
    if (merged) goto done;
    result = 1;

    if (order == 0) {
        free(a); free(b); free(c);
    } else if (order == 1) {
        free(c); free(b); free(a);
    } else {
        free(a); free(c); free(b);
    }
    a = 0; b = 0; c = 0;
    merged = malloc(need);
    meminfo(&after);
    if (merged && (uintptr_t)merged == start && after.heap_break == before.heap_break)
        result |= 2;
    if (merged) fill(merged, need, 61);
    if (check(left, 37, 11) && check(right, 53, 29) && (!tail || check(tail, 16, 47)))
        result |= 4;

done:
    free(merged); free(tail); free(right);
    free(c); free(b); free(a); free(left);
    meminfo(&after);
    if (after.heap_break == initial.heap_break) result |= 8;
    return result;
}

static int dir_make_file(const char *name)
{
    FILE *f = fopen(name, "wb");
    int wrote, closed;
    if (!f) return 0;
    wrote = fputc(65, f);
    closed = fclose(f);
    return wrote == 65 && closed == 0;
}

static int dir_bit(const struct dirent *entry, const char *first, const char *second)
{
    if (!entry) return 0;
    if (strcmp(entry->d_name, first) == 0) return 1;
    if (strcmp(entry->d_name, second) == 0) return 2;
    return 4;
}

static int dir_rest(DIR *dir, const char *first, const char *second, int seen)
{
    struct dirent *entry;
    int bit;
    while ((entry = readdir(dir)) != 0) {
        bit = dir_bit(entry, first, second);
        if (bit == 4 || (seen & bit)) return 0;
        seen |= bit;
    }
    entry = readdir(dir);
    return seen == 3 && entry == 0;
}
#endif

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
            leading = self.pick(('', '', ' ', '  ', '\t'))
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
                # Without format whitespace, %c reads the padding as part of
                # its field. Keep the whole field within the conversion width.
                if not fmt[-1:].isspace():
                    tok = (leading + tok)[:n]
                    leading = ''
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
            text += leading + tok
        if self.r.random() < 0.2 and text and not counted:
            i = self.r.randrange(len(text))
            if i == 0 or text[i - 1] not in 'xX':
                bad = self.pick('q;')
                # Keep separators intact so a later conversion cannot read
                # an earlier token that overflows its destination type.
                if not text[i].isspace() and text[i] not in ',:':
                    text = text[:i] + bad + text[i + 1:]
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

    def t_heap_pressure(self, tid):
        sizes = [self.r.randint(96, 480) for _ in range(8)]
        seed = self.r.randrange(256)
        return ['char *p[8], *q;', 'int i, j, ok = 1;',
                'int sizes[8] = { %s };' % ', '.join(map(str, sizes)),
                'int seed = %d;' % seed,
                '#ifdef _DCC_', 'volatile size_t impossible = 60000u;',
                '#else', 'volatile size_t impossible = (size_t)-1;', '#endif',
                'for (i = 0; i < 8; ++i) {',
                '    p[i] = malloc(sizes[i]);',
                '    if (p[i]) fill(p[i], sizes[i], seed + i);',
                '    else ok = 0;',
                '}',
                'for (i = 1; i < 8; i += 2) {',
                '    free(p[i]);',
                '    sizes[i] += 17;',
                '    p[i] = malloc(sizes[i]);',
                '    if (p[i]) fill(p[i], sizes[i], seed + i);',
                '    else ok = 0;',
                '    for (j = 0; j < 8; ++j)',
                '        if (p[j] && !check(p[j], sizes[j], seed + j)) ok = 0;',
                '}',
                'if (p[0]) {',
                '    q = realloc(p[0], impossible);',
                '    if (q) { p[0] = q; ok = 0; }',
                '    if (!check(p[0], sizes[0], seed)) ok = 0;',
                '}',
                'for (i = 0; i < 8; ++i) free(p[i]);',
                'tag(%d, "heap pressure"); pd(ok); nl();' % tid]

    def t_heap_coalescing(self, tid):
        sizes = [self.r.randint(96, 640) for _ in range(3)]
        order = self.r.randrange(3)
        return ['tag(%d, "heap coalescing");' % tid,
                '#ifdef _DCC_',
                'pd(heap_coalesce(%d, %d, %d, %d));' % (*sizes, order),
                '#else',
                '/* All four target heap invariants should hold. */',
                'pd(15);',
                '#endif', 'nl();']

    def t_float_edges(self, tid):
        magnitude = self.pick((0, 1, 0x007fffff, 0x00800000, 0x3f7fffff,
                               0x3f800000, 0x3f800001, 0x7f7fffff, 0x7f800000))
        bits = magnitude | self.pick((0, 0x80000000))
        return ['volatile float x = bitsf(0x%08lxUL);' % bits,
                'tag(%d, "float edges");' % tid,
                'pf(fabsf(x)); sp();',
                'pd(!!isfinite(x)); sp(); pd(!!isinf(x)); sp(); pd(!!signbit(x)); nl();']

    def t_descriptor_file(self, tid):
        boundary = self.pick((128, 16384))
        patch = self.r.randrange(128, 256)
        return ['char block[128], sample[3];',
                'int fd, i, ok = 1;', 'FILE *f;',
                'long boundary = %dL;' % boundary,
                'memset(block, 0x1a, sizeof block);',
                'fd = open("FD.DAT", O_CREAT | O_TRUNC | O_RDWR, 0600);',
                'if (fd < 0) ok = 0;',
                'else {',
                '    for (i = 0; i <= boundary / 128; ++i)',
                '        if (write(fd, block, sizeof block) != sizeof block) ok = 0;',
                '    if (lseek(fd, boundary - 1, SEEK_SET) != boundary - 1) ok = 0;',
                '    block[0] = (char)%d; block[1] = 0;' % patch,
                '    if (write(fd, block, 2) != 2) ok = 0;',
                '    if (close(fd) != 0) ok = 0;',
                '}',
                'fd = open("FD.DAT", O_RDONLY);',
                'if (fd < 0) ok = 0;',
                'else {',
                '    if (lseek(fd, boundary - 1, SEEK_SET) != boundary - 1) ok = 0;',
                '    if (read(fd, sample, sizeof sample) != sizeof sample) ok = 0;',
                '    else if ((unsigned char)sample[0] != %d || sample[1] || sample[2] != 0x1a) ok = 0;' % patch,
                '    if (close(fd) != 0) ok = 0;',
                '}',
                'f = fopen("FD.DAT", "rb");',
                'if (!f) ok = 0;',
                'else {',
                '    if (fseek(f, boundary - 1, SEEK_SET) != 0) ok = 0;',
                '    if (fgetc(f) != %d || fgetc(f) != 0 || fgetc(f) != 0x1a) ok = 0;' % patch,
                '    if (fclose(f) != 0) ok = 0;',
                '}',
                'if (remove("FD.DAT") != 0) ok = 0;',
                'tag(%d, "descriptor boundary"); pd(ok); nl();' % tid]

    def t_append_flush(self, tid):
        size = self.pick((128, 256))
        data = self.word(8) or 'append'
        offset = self.r.randrange(size)
        return ['static const char suffix[] = %s;' % c_str(data),
                'char block[%d], actual[sizeof suffix - 1];' % size,
                'FILE *writer, *reader;', 'int ready = 0, visible = 0, prefix = 0;',
                'memset(block, 65, sizeof block);',
                'writer = fopen("APPEND.DAT", "wb");',
                'if (writer) {',
                '    ready = fwrite(block, 1, sizeof block, writer) == sizeof block;',
                '    if (fclose(writer) != 0) ready = 0;',
                '}',
                'writer = fopen("APPEND.DAT", "ab");',
                'if (writer) {',
                '    if (fseek(writer, %dL, SEEK_SET) != 0) ready = 0;' % offset,
                '    if (fwrite(suffix, 1, sizeof actual, writer) != sizeof actual) ready = 0;',
                '    if (fflush(writer) != 0) ready = 0;',
                '    reader = fopen("APPEND.DAT", "rb");',
                '    if (reader) {',
                '        prefix = fread(block, 1, sizeof block, reader) == sizeof block;',
                '        if (prefix) {',
                '            int i;',
                '            for (i = 0; i < sizeof block; ++i) if (block[i] != 65) prefix = 0;',
                '            visible = fread(actual, 1, sizeof actual, reader) == sizeof actual',
                '                   && memcmp(actual, suffix, sizeof actual) == 0;',
                '        }',
                '        if (fclose(reader) != 0) visible = 0;',
                '    }',
                '    if (fclose(writer) != 0) ready = 0;',
                '} else ready = 0;',
                'if (remove("APPEND.DAT") != 0) ready = 0;',
                'tag(%d, "append flush"); pd(ready); sp(); pd(prefix); sp(); pd(visible); nl();' % tid]

    def t_text_eof(self, tid):
        offset = self.pick((1, 127, 128, 129))
        before, after = self.r.sample(range(65, 91), 2)
        return ['char block[256], text[4] = { 0 };', 'FILE *f;',
                'int ready = 0, got_text = 0, eof = 0, error = 0;',
                'int binary_marker = -2, trailing = -2;',
                'memset(block, 46, sizeof block);',
                'block[%d] = %d; block[%d] = 26; block[%d] = %d;' %
                (offset - 1, before, offset, offset + 1, after),
                'f = fopen("TEXT.DAT", "wb");',
                'if (f) {',
                '    ready = fwrite(block, 1, sizeof block, f) == sizeof block;',
                '    if (fclose(f) != 0) ready = 0;',
                '}',
                'f = fopen("TEXT.DAT", "r");',
                'if (f) {',
                '    if (fseek(f, %dL, SEEK_SET) == 0) {' % (offset - 1),
                '        got_text = fgets(text, sizeof text, f) != 0;',
                '        eof = !!feof(f); error = !!ferror(f);',
                '        #ifndef _DCC_',
                '        /* Model the CP/M line read using the actual bytes returned by the host. */',
                '        if (got_text && text[1] == 26 && !eof && !error) { text[1] = 0; eof = 1; }',
                '        #endif',
                '    }',
                '    if (fclose(f) != 0) ready = 0;',
                '}',
                'f = fopen("TEXT.DAT", "rb");',
                'if (f) {',
                '    if (fseek(f, %dL, SEEK_SET) == 0) {' % offset,
                '        binary_marker = fgetc(f); trailing = fgetc(f);',
                '    }',
                '    if (fclose(f) != 0) ready = 0;',
                '}',
                'if (remove("TEXT.DAT") != 0) ready = 0;',
                'tag(%d, "text EOF"); pd(ready); sp(); pd(got_text); sp(); pstr(text); sp();' % tid,
                'pd(eof); sp(); pd(error); sp(); pd(binary_marker); sp(); pd(trailing); nl();']

    def t_descriptor_recovery(self, tid):
        slot = self.r.randrange(8)
        return ['char data[128], byte;',
                'int fds[8], i, extra, setup = 0, exhausted = 0, reopened = 0, positions = 1;',
                'FILE *f;',
                'for (i = 0; i < sizeof data; ++i) data[i] = (char)i;',
                'f = fopen("LIMIT.DAT", "wb");',
                'if (f) {',
                '    setup = fwrite(data, 1, sizeof data, f) == sizeof data;',
                '    if (fclose(f) != 0) setup = 0;',
                '}',
                '#ifndef _DCC_',
                '/* Give the host the target budget of eight files plus standard I/O. */',
                'struct rlimit saved, limit;',
                'int limited = 0;',
                'if (getrlimit(RLIMIT_NOFILE, &saved) == 0) {',
                '    limit = saved; limit.rlim_cur = 11;',
                '    limited = setrlimit(RLIMIT_NOFILE, &limit) == 0;',
                '}',
                'if (!limited) setup = 0;',
                '#endif',
                'for (i = 0; i < 8; ++i) {',
                '    fds[i] = open("LIMIT.DAT", O_RDONLY);',
                '    if (fds[i] < 0 || lseek(fds[i], (long)(i * 3), SEEK_SET) != i * 3) positions = 0;',
                '}',
                'errno = 0; extra = open("LIMIT.DAT", O_RDONLY);',
                'exhausted = extra == -1 && errno == EMFILE;',
                'if (extra >= 0) close(extra);',
                'if (fds[%d] >= 0 && close(fds[%d]) == 0) {' % (slot, slot),
                '    fds[%d] = -1;' % slot,
                '    extra = open("LIMIT.DAT", O_RDONLY);',
                '    if (extra >= 0) {',
                '        reopened = read(extra, &byte, 1) == 1 && byte == 0;',
                '        if (close(extra) != 0) reopened = 0;',
                '    }',
                '}',
                'for (i = 0; i < 8; ++i) {',
                '    if (fds[i] >= 0) {',
                '        if (read(fds[i], &byte, 1) != 1 || byte != i * 3) positions = 0;',
                '        if (close(fds[i]) != 0) positions = 0;',
                '    }',
                '}',
                '#ifndef _DCC_',
                'if (limited && setrlimit(RLIMIT_NOFILE, &saved) != 0) setup = 0;',
                '#endif',
                'if (remove("LIMIT.DAT") != 0) setup = 0;',
                'tag(%d, "descriptors"); pd(setup); sp(); pd(exhausted); sp();' % tid,
                'pd(reopened); sp(); pd(positions); nl();']

    def t_float_rounding(self, tid):
        return ['volatile float even = bitsf(0x3f800000UL), odd = bitsf(0x3f800001UL);',
                'volatile float half_ulp = bitsf(0x33800000UL), half = bitsf(0x3f000000UL);',
                'volatile float small = bitsf(0x00800000UL), large = bitsf(0x7f7fffffUL);',
                'volatile float result;',
                'tag(%d, "rounding");' % tid,
                'result = even + half_ulp; pf(result); sp();',
                'result = odd + half_ulp; pf(result); sp();',
                'result = small * half;',
                '#ifndef _DCC_',
                '/* DCC multiply flushes subnormal results to signed zero. */',
                'if (fbits(result) == 0x00400000UL) result = 0.0f;',
                '#endif',
                'pf(result); sp(); pf(ldexpf(small, -1)); sp();',
                'result = large + large; pf(result); nl();']

    def t_stream_flags(self, tid):
        size = self.pick((128, 256))
        return ['char data[%d];' % size, 'FILE *f;', 'int ready = 0, c;',
                'memset(data, 65, sizeof data);',
                'f = fopen("FLAGS.DAT", "wb");',
                'if (f) {',
                '    ready = fwrite(data, 1, sizeof data, f) == sizeof data;',
                '    if (fclose(f) != 0) ready = 0;',
                '}',
                'f = fopen("FLAGS.DAT", "rb");',
                'tag(%d, "stream flags"); pd(ready); sp();' % tid,
                'if (f) {',
                '    pd(fseek(f, 0L, SEEK_END)); sp();',
                '    c = fgetc(f); pd(c == EOF); sp(); pd(!!feof(f)); sp(); pd(!!ferror(f)); sp();',
                '    c = ungetc(90, f); pd(c); sp(); pd(!!feof(f)); sp(); pd(fgetc(f)); sp();',
                '    c = fgetc(f); pd(c == EOF); sp(); pd(!!feof(f)); sp();',
                '    pd(fseek(f, 0L, SEEK_SET)); sp(); pd(!!feof(f)); sp(); pd(fgetc(f)); sp();',
                '    c = fputc(42, f); pd(c == EOF); sp(); pd(!!ferror(f)); sp();',
                '    clearerr(f); pd(!!feof(f)); sp(); pd(!!ferror(f)); sp();',
                '    pd(fgetc(f)); sp(); pd(fclose(f)); sp();',
                '}',
                'pd(remove("FLAGS.DAT")); nl();']

    def t_directory_scan(self, tid):
        files = ['OR1A.TMP', 'OR2A.TMP', 'OR1B.TMP', 'OR2B.TMP']
        self.r.shuffle(files)
        return ['#ifdef _DCC_',
                'DIR *a, *b; struct dirent *entry; int first, setup = 1;',
                'int busy = 0, resumed = 0, switched = 0, restarted = 0;'] + [
                'if (!dir_make_file(%s)) setup = 0;' % c_str(name) for name in files] + [
                'a = opendir("OR?A.TMP");',
                'if (a) {',
                '    entry = readdir(a);',
                '    first = dir_bit(entry, "OR1A.TMP", "OR2A.TMP");',
                '    errno = 0; b = opendir("OR?B.*");',
                '    busy = b == 0 && errno == EMFILE;',
                '    if (b) closedir(b);',
                '    if (first == 1 || first == 2) resumed = dir_rest(a, "OR1A.TMP", "OR2A.TMP", first);',
                '    if (closedir(a) != 0) resumed = 0;',
                '}',
                'b = opendir("OR?B.*");',
                'if (b) {',
                '    switched = dir_rest(b, "OR1B.TMP", "OR2B.TMP", 0);',
                '    if (closedir(b) != 0) switched = 0;',
                '}',
                'a = opendir("OR?A.TMP");',
                'if (a) {',
                '    restarted = dir_rest(a, "OR1A.TMP", "OR2A.TMP", 0);',
                '    if (closedir(a) != 0) restarted = 0;',
                '}',
                ] + ['if (remove(%s) != 0) setup = 0;' % c_str(name) for name in files] + [
                'tag(%d, "directory scan");' % tid,
                'pd(setup); sp(); pd(busy); sp(); pd(resumed); sp(); pd(switched); sp(); pd(restarted);',
                '#else', 'tag(%d, "directory scan");' % tid,
                '/* CP/M permits one active wildcard search; validate against its contract. */',
                'ps("1 1 1 1 1");', '#endif', 'nl();']

    def t_format_edges(self, tid, floatio=False, longio=False):
        width = self.pick((-12, -8, 8, 12))
        precision = self.r.randint(1, 6)
        limit = self.r.randint(1, 8)
        fmt = '%%%d.%dd' % (width, precision)
        values = ['42']
        if longio:
            fmt += ':%ld'
            values.append(c_long(self.r.randint(-1000000000, 1000000000)))
        if floatio:
            fmt += ':%.2f'
            values.append(c_float(self.r.randint(-1000, 1000) / 8))
        call_args = ', '.join([c_str(fmt), *values])
        return ['char b[64];', 'int n, a = -1, c;', 'FILE *f;',
                'memset(b, 35, sizeof b);',
                'n = sprintf(b, %s);' % call_args,
                'tag(%d, "format sprintf"); pd(n); sp(); pstr(b); nl();' % tid,
                'memset(b, 35, sizeof b);',
                'n = snprintf(b, %d, %s);' % (limit, call_args),
                'tag(%d, "format edges"); pd(n); sp(); pmem(b, 16); sp();' % tid,
                'f = fopen("SCAN.DAT", "w");',
                'if (f) { fputs("42X", f); fclose(f); }',
                'f = fopen("SCAN.DAT", "r");',
                'if (f) {',
                '    n = fscanf(f, "%d%d", &a, &c);',
                '    pd(n); sp(); pd(a); sp(); pd(fgetc(f));',
                '    fclose(f);',
                '}',
                'remove("SCAN.DAT"); nl();']

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
        append = mode.startswith('a')
        if append:
            # DCC writes through. With buffered append, the host can report
            # positions relative to the old seek offset until the write lands
            # at EOF. Exercise buffered append separately in t_append_flush.
            out += ['#ifndef _DCC_',
                    'if (setvbuf(f, NULL, _IONBF, 0) != 0) { fclose(f); ps("setvbuf failed"); nl(); return; }',
                    '#endif']
        # Where an append stream starts, for reading and for ftell, is
        # implementation-defined: wait for a write or seek to fix it.
        pos = None if append else 0
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
                if append:
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
        expected = val or 1
        return ['volatile int n = 0;', 'int r;', 'switch (setjmp(jb)) {',
                'case 0: n = %d; jdeep(%d, %d); n = -99; r = 0; break;' % (depth + 10, depth, val),
                'case %d: r = %d; break;' % (expected, expected),
                'default: r = -99; break;', '}',
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
        extra = ('t_heap_pressure', 't_float_edges', 't_descriptor_file', 't_format_edges',
                 't_append_flush', 't_text_eof', 't_descriptor_recovery',
                 't_float_rounding', 't_stream_flags', 't_directory_scan', 't_heap_coalescing')
        for tid in range(1, count + len(extra) + 1):
            name = self.pick(names) if tid <= count else extra[tid - count - 1]
            lines = getattr(self, name)(tid)
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


def runtime_label_lines():
    source = (REPO / 'DCCRTL.MAC').read_text(encoding='latin-1')
    return {match.group(1).upper(): number
            for number, line in enumerate(source.splitlines(), 1)
            for match in [re.match(r'([_a-zA-Z]\w*):', line)] if match}


def runtime_label_hits(build):
    with (build / 'rtl-pc.csv').open(newline='') as profile:
        counts = {int(row['pc']): int(row['count']) for row in csv.DictReader(profile)}
    with (build / 'RTLMIN.SYM').open(encoding='latin-1') as symbols:
        for line in symbols:
            fields = line.split()
            if len(fields) < 3 or fields[2] != 'code':
                continue
            name, offset = fields[:2]
            # dccmake links RTLMIN first at the CP/M COM load address.
            count = counts.get(0x100 + int(offset, 16), 0)
            if count:
                yield name.upper(), count


def report_rtl_coverage(work, seeds):
    labels = runtime_label_lines()
    builds_hit, counts = Counter(), Counter()
    builds = 0
    for seed in seeds:
        for profile in (Path(work) / str(seed)).glob('**/rtl-pc.csv'):
            if not (profile.parent / 'RTLMIN.SYM').is_file():
                continue
            builds += 1
            for name, count in runtime_label_hits(profile.parent):
                if name in labels:
                    builds_hit[name] += 1
                    counts[name] += count
    path = Path(work) / 'rtl-coverage.tsv'
    with path.open('w', newline='') as report:
        writer = csv.writer(report, delimiter='\t', lineterminator='\n')
        writer.writerow(('source_line', 'label', 'builds_hit', 'executions'))
        for name in sorted(counts, key=labels.get):
            writer.writerow((labels[name], name, builds_hit[name], counts[name]))
    print('%d DCCRTL labels reached across %d emulator profiles (%s)' % (len(counts), builds, path))
    return bool(counts)


def dcc_output(source, build, peep, args, floatio=True, longio=True):
    shutil.rmtree(build, ignore_errors=True)
    cmd = [str(REPO / 'dccmake'), 'dcc-input=%s' % source, 'dcc-output=T',
           'dcc-build-dir=%s' % build, 'dcc-peep=%s' % ('true' if peep else 'false'),
           'dcc-stack-bytes=2048', 'dcc-floatio=%s' % str(floatio).lower(),
           'dcc-flongio=%s' % str(longio).lower(),
           'ntvcm-tool=%s' % args.emulator]
    try:
        r = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, stdin=subprocess.DEVNULL,
                           timeout=args.build_timeout)
    except subprocess.TimeoutExpired:
        return None, 'build timed out'
    if r.returncode != 0:
        log = [ln for ln in (r.stdout + r.stderr).splitlines() if not ln.startswith('+')]
        errors = [ln for ln in log if any(term in ln.lower() for term in
                                         ('error', 'fatal', 'undefined symbol'))
                  and not ln.startswith('No Fatal error')]
        return None, 'build failed: ' + ((errors or log or ['?'])[0].replace(str(source) + ':', ''))[:160]
    emulator_cmd = [args.emulator, '-s:0']
    if args.rtl_coverage:
        emulator_cmd.append('-g:rtl-pc.csv')
    try:
        run = subprocess.run([*emulator_cmd, 'T.COM'], cwd=build, capture_output=True,
                             timeout=args.timeout, stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        return None, 'run timed out'
    if run.returncode != 0:
        detail = run.stderr.decode('latin-1').strip().splitlines()
        why = 'emulator exited with status %d' % run.returncode
        if detail:
            why += ': ' + detail[0][:160]
        return None, why
    if args.rtl_coverage and not (build / 'rtl-pc.csv').is_file():
        return None, 'emulator did not write its per-PC profile'
    return run.stdout.decode('latin-1').replace('\r', ''), ''


def close_enough(got, want):
    """Transcendental results: the documented 5-6 significant digits."""
    try:
        g = struct.unpack('<f', struct.pack('<I', int(got, 16)))[0]
        w = struct.unpack('<f', struct.pack('<I', int(want, 16)))[0]
    except (ValueError, struct.error):
        return False
    if math.isnan(w) or math.isnan(g):
        return math.isnan(w) and math.isnan(g)
    if math.isinf(w) or math.isinf(g):
        return g == w
    if w == 0:
        return g == 0 and math.copysign(1.0, g) == math.copysign(1.0, w)
    return abs(g - w) <= 3e-5 * abs(w)


def within_ulp(got, want, ulp):
    """A ^ line: its first field, float bits, within ulp; the rest exact."""
    g, w = got.split(' ', 1) + [''], want.split(' ', 1) + ['']
    try:
        gi, wi = int(g[0], 16), int(w[0], 16)
    except ValueError:
        return False
    # Keep zero, infinity and NaN exact. Only finite nonzero values
    # are subject to the parser's rounding tolerance.
    if ((gi ^ wi) & 0x80000000 or
            not 0 < (gi & 0x7fffffff) < 0x7f800000 or
            not 0 < (wi & 0x7fffffff) < 0x7f800000):
        return False
    return abs(gi - wi) <= ulp and g[1] == w[1]


def compare(got, want, ulp=2):
    """Mismatching test lines as (id, name, got, want)."""
    bad = []
    glines, wlines = got.splitlines(), want.splitlines()
    for i, w in enumerate(wlines):
        g = glines[i] if i < len(glines) else '<missing>'
        if g == w:
            continue
        m = re.match(r'(\d+) ([~^]?)([^:]*): (.*)$', w)
        if m and m.group(2):
            prefix = w[:m.start(4)]
            if g.startswith(prefix):
                result = g[len(prefix):]
                if m.group(2) == '~' and close_enough(result, m.group(4)):
                    continue
                if m.group(2) == '^' and ulp and within_ulp(result, m.group(4), ulp):
                    continue
        bad.append((m.group(1) if m else '?', (m.group(3) if m else w).split()[0], g, w))
    if len(glines) > len(wlines):
        bad.append(('?', 'extra output', glines[len(wlines)], ''))
    return bad


def run_seed(seed, args):
    work = Path(args.work) / str(seed)
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    source = work / 't.c'
    source.write_text(Gen(seed).program(args.tests), encoding='latin-1')
    profiles = [(work, True, True)]
    if args.io_matrix:
        for name, floatio, longio in (('plain', False, False), ('float', True, False),
                                     ('long', False, True), ('float-long', True, True)):
            profile = work / name
            profile.mkdir()
            lines = Gen(seed).t_format_edges(1, floatio, longio)
            (profile / 't.c').write_text(PRELUDE + '\nint main(void)\n{\n' +
                                         '\n'.join('    ' + line for line in lines) +
                                         '\n    return 0;\n}\n', encoding='latin-1')
            profiles.append((profile, floatio, longio))
    bad = []
    for profile, floatio, longio in profiles:
        source = profile / 't.c'
        prefix = '' if profile == work else profile.name + '/'
        want, why = host_expected(source, profile, args.timeout)
        if want is None:
            return seed, 'INVALID', prefix + why, []
        for peep in (False, True):
            cfg = 'peep' if peep else 'nopeep'
            got, why = dcc_output(source, profile / cfg, peep, args, floatio, longio)
            if got is None:
                return seed, 'BUILD', prefix + cfg + ': ' + why, []
            bad.extend((prefix + cfg, row) for row in compare(got, want, args.ulp))
    if not bad:
        if not args.keep and not args.rtl_coverage:
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
    with progress_terminal() as terminal, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        total = len(seeds)
        show_progress = total > 0 and terminal is not None
        update_progress, report = progress_reporter(total, terminal) if show_progress else (None, print)
        results = pool.map(lambda s: run_seed(s, args), seeds)
        if show_progress:
            update_progress(0)
        for completed, (seed, kind, why, bad) in enumerate(results, 1):
            if show_progress and completed % 10 == 0 and completed < total:
                update_progress(completed)
            counts[kind] = counts.get(kind, 0) + 1
            if kind in ('INVALID', 'BUILD'):
                report('seed %d: %s %s' % (seed, kind, why))
            for cfg, (tid, name, got, want) in bad:
                first = name not in names
                names.setdefault(name, []).append((seed, tid))
                if first or args.verbose:
                    report('seed %d test %s %s [%s]\n  dcc:  %s\n  host: %s' % (
                        seed, tid, name, cfg, got[:200], want[:200]))
        if show_progress:
            update_progress(total)
            print(file=terminal, flush=True)
    print('\n%d programs: %s' % (len(seeds), ', '.join('%s %d' % (k, counts[k]) for k in sorted(counts))))
    for name, where in sorted(names.items()):
        print('  %-12s %4d mismatches, first seed %d test %s' % (name, len(where), where[0][0], where[0][1]))
    coverage_ok = not args.rtl_coverage or report_rtl_coverage(args.work, seeds)
    return 1 if not coverage_ok or any(kind != 'ok' for kind in counts) else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--seeds', default='1-50')
    parser.add_argument('--tests', type=int, default=40, help='random tests per program, plus additional boundary cases')
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    parser.add_argument('--work', default=str(REPO / 'build' / 'rtl-oracle-fuzz'))
    parser.add_argument('--emulator', default='ntvcm')
    parser.add_argument('--timeout', type=float, default=30)
    parser.add_argument('--build-timeout', type=float, default=300)
    parser.add_argument('--ulp', type=int, default=2,
                        help='accept strtod results this many float ulps away (default: 2)')
    parser.add_argument('--io-matrix', action='store_true',
                        help='also fuzz formatted I/O in all four float/long profiles')
    parser.add_argument('--rtl-coverage', action='store_true',
                        help='keep builds and write a DCCRTL label-hit report using NTVMC per-PC profiling')
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
