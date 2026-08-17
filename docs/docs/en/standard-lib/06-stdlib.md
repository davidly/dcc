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

The allocator uses a first-fit heap walk with two-byte packed boundary tags at
the start and end of each block. Freeing a block coalesces it with adjacent free
neighbours (including blocks freed via `realloc(p, 0)` and the old block
released by a growing `realloc`), which keeps fragmentation down. `realloc`
also grows in place at the heap top or into an immediately following free
block. The heap grows on demand between the end of BSS and the stack. On CP/M
this space is bounded by the program's TPA: code, data, runtime support, heap,
and stack all share the same transient program area.

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

`atoi`/`atol` skip the full C whitespace set (space and bytes `\t` through
`\r`), accept an optional `+`/`-` sign, then consume decimal digits; conversion
stops at the first non-digit. Overflow wraps modulo the type width.

```c
int  n = atoi("  -123xyz");   /* -123  */
long m = atol("  -123456");   /* -123456L */
```

`strtol`/`strtoul` are the full C89 conversions. They skip leading whitespace,
accept an optional sign, honour a `0x`/`0X` prefix for base 16 and a leading `0`
for base 8 when `base` is 0, and accept digits/letters up to `base`-1 for any
base from 2 to 36. The unused tail is reported through `*end` when `end` is
non-`NULL`. On overflow they clamp to `LONG_MAX`/`LONG_MIN` (or `ULONG_MAX`) and
set `errno` to `ERANGE`. A `0x` prefix is recognized only when a hexadecimal
digit follows it; for `"0x"` or `"0xG"` the leading zero is converted and
`end` points at the `x`, without changing `errno`.

```c
char *end;
long  v = strtol("  -0x1Ag", &end, 0);            /* v = -26, *end = 'g'  */
unsigned long u = strtoul("4294967295", NULL, 10); /* ULONG_MAX */
```

## Multibyte and wide characters

DCC uses a fixed single-byte execution encoding (`MB_CUR_MAX == 1`).
Byte values `0x00` through `0xFF` map to equal-valued 16-bit `wchar_t` values.
A wider value is unrepresentable: `wctomb` returns `-1` without writing a
truncated byte, and `wcstombs` returns `(size_t)-1` at the offending element.
`wcstombs` may already have stored a representable prefix, as permitted by C,
but never stores the truncated offending value. With `n == 0`, it examines and
writes no elements and returns zero.

`atof` is available as a DCC C Compiler extension: it is declared as `float atof(const char *nptr)`
and returns IEEE 754 single precision. C89 `atof` normally returns `double`, which DCC C Compiler
does not have. It accepts ordinary decimal text with an optional exponent, plus the case-insensitive
spellings `nan`, `inf`, and `infinity`. Overflow returns signed infinity; underflow returns signed zero.
`strtod` uses the same parser and reports the first unconsumed byte through
`endptr`. Numeric overflow and underflow set `errno` to `ERANGE`; explicit
infinity/NaN spellings and an exact zero with a large exponent are not range
errors.

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

`exec()` and `execv()` replace the current program through the CP/M command-tail
area. DCC accepts the full 127-byte payload in `0x81..0xFF`; the length byte is
authoritative, so the conventional trailing CR is omitted only at that exact
maximum. A 128th byte returns `-1` with `errno == E2BIG`. `exec()` validates
and copies its caller-owned source into private staging before opening the
image, abandoning the caller stack, clearing FCBs, or writing the destination
tail, so stack-local and overlapping low-memory sources are safe.

The executable path must be an unambiguous CP/M 8.3 name: an optional
`A:` through `P:` drive, one to eight filename bytes, and an optional one to
three byte filetype. DCC appends `.COM` when the filetype is absent. Invalid
syntax returns `EINVAL`, a failed open returns `ENOENT`, and an executable whose
128-byte-record-rounded image cannot fit below the loader's reserved high-memory
stack/FCB/trampoline ranges returns `EFBIG`.

The BDOS function 35 record count is also the loader's exact read contract. The
high-memory trampoline performs exactly that many successful sequential reads,
never reads a newly grown extra record, and warm-boots rather than jumping to a
partial image if any approved read returns a nonzero status.

The startup parser intentionally keeps direct CP/M behavior: bytes through
ASCII space delimit arguments, while quote and backslash are literal bytes and
have no escaping role. Therefore `execv()` can round-trip only nonempty
arguments containing bytes above ASCII space. It returns `-1` with
`errno == EINVAL` for empty arguments or arguments containing spaces, tabs, or
other delimiter bytes rather than silently changing `argv`.

The first two command-tail words seed CP/M's default FCB1 and FCB2. Their
delimiter rule is identical to startup argument parsing: every byte through
ASCII space, including tab and control whitespace, is a delimiter.

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
