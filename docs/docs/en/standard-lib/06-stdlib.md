# Memory and utilities (`stdlib.h`)

Include [`stdlib.h`](06-stdlib.md). This header covers dynamic memory,
string-to-number conversion, integer arithmetic helpers, searching and sorting,
process control, and pseudo-random numbers.

## Types and Macros

<!-- STDLIB-SYMBOL-TABLE: all -->

## Functions

<!-- STDLIB-FUNCTION-TABLE: all -->

## Runtime model

The standard functions in this header are runtime-backed. DCC C Compiler also declares a
small set of CP/M and Z80 extensions here (`bdos`, `inp`, and `outp`); those are
documented with the CP/M services rather than treated as portable C APIs.

## Dynamic memory

The allocator is a first-fit free list with a 3-byte block header. Freeing a
block coalesces it with adjacent free neighbours (including blocks freed via
`realloc(p, 0)` and the old block released by a growing `realloc`), which keeps
fragmentation down. The heap grows on demand between the end of BSS and the
stack. On CP/M this space is bounded by the program's TPA: code, data, runtime
support, heap, and stack all share the same transient program area.

```c
char *p = malloc(256);
if (!p) { fputs("out of memory\n", stderr); exit(EXIT_FAILURE); }
p = realloc(p, 512);        /* old contents preserved */
free(p);
```

`realloc` follows the standard rules: `realloc(NULL, n)` behaves like
`malloc(n)`, and `realloc(p, 0)` frees `p` and returns `NULL`.

!!! note "Size cost"
    `malloc`/`calloc` link integer multiply/divide/modulo helpers for size
    arithmetic, and `strdup` inherits the whole `malloc` chain. See the
    [appendix](../appendix/01-dccrtlstrip.md).

## Conversion

`atoi`/`atol` skip leading spaces/tabs, accept an optional `+`/`-` sign, then
consume decimal digits; conversion stops at the first non-digit. Overflow wraps
modulo the type width.

```c
int  n = atoi("  -123xyz");   /* -123  */
long m = atol("  -123456");   /* -123456L */
```

`strtol`/`strtoul` are the full C89 conversions. They skip leading whitespace,
accept an optional sign, honour a `0x`/`0X` prefix for base 16 and a leading `0`
for base 8 when `base` is 0, and accept digits/letters up to `base`-1 for any
base from 2 to 36. The unused tail is reported through `*end` when `end` is
non-`NULL`. On overflow they clamp to `LONG_MAX`/`LONG_MIN` (or `ULONG_MAX`) and
set `errno` to `ERANGE`.

```c
char *end;
long  v = strtol("  -0x1Ag", &end, 0);            /* v = -26, *end = 'g'  */
unsigned long u = strtoul("4294967295", NULL, 10); /* ULONG_MAX */
```

`atof` is available as a DCC C Compiler extension: it is declared as `float atof(const char *nptr)`
and returns IEEE 754 single precision. C89 `atof` normally returns `double`, which DCC C Compiler
does not have. It accepts ordinary decimal text with an optional exponent, plus the case-insensitive
spellings `nan`, `inf`, and `infinity`. Overflow returns signed infinity; underflow returns signed zero.

## Integer arithmetic helpers

`div` returns a `div_t` with `quot` and `rem` members; `ldiv` returns an
`ldiv_t` with 32-bit members. Signed division truncates toward zero; the
remainder has the same sign as the numerator.

```c
div_t  d  = div(-7, 3);          /* d.quot == -2, d.rem == -1 */
ldiv_t ld = ldiv(200000L, 7L);
```

## Searching and sorting

Both take the standard comparator: `cmp(a, b)` returns negative if `a` sorts
before `b`, zero if equal, positive if after. `qsort` uses an in-place,
non-recursive Shell sort, so it is **not stable**; `bsearch` requires the array
to be sorted by the same comparator. See [Worked examples](../12-examples.md) for
complete programs.

## Process control

The exit code is surfaced through CP/M 3.0 BDOS call 108, which emulators such
as ntvcm reflect in their own process exit code. Returning a value from `main`
has the same effect.

## Pseudo-random numbers

`RAND_MAX` is 32767.

```c
srand(1);
int roll = rand() % 6 + 1;     /* a die roll */
```

The runtime generator is a 16-bit xorshift with parameters 7, 9, and 8. The
state is 16-bit, `srand(seed)` stores the state directly, and `rand()` clears
bit 15 of the updated state so the result stays in the C89 `0 .. RAND_MAX`
range. In C-equivalent form:

```c
static unsigned int s_rnd = 1;

void srand(unsigned int seed)
{
    s_rnd = seed;
}

int rand(void)
{
    s_rnd ^= s_rnd << 7;
    s_rnd ^= s_rnd >> 9;
    s_rnd ^= s_rnd << 8;
    return (int)(s_rnd & 0x7fff);
}
```

That deterministic sequence is useful for benchmarks: if another CP/M compiler
uses the same C equivalent, tests that depend on `rand()` can compare runtime
library and code-generation performance without being skewed by different
pseudo-random sequences.
