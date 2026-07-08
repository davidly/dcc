# dcc C89 library reference

The runtime is `DCCRTL.MAC`; the compiler maps well-known C names to its short
entry points (e.g. `memcpy`→`__mcpy`). Because C89 lets you call an undeclared
function (implicit `int`), referencing something the runtime doesn't provide
fails at **link** time (`unresolved external`), not compile time. The shipped
headers are a hand-written minimal subset, so an entry point can exist in the
runtime without a prototype (the heap symbols `_brk`/`_hlimit` are the deliberate
case) — declare those yourself.

**Assume standard C89 behaviour for every function listed as implemented.** This
file documents only the deviations: the subset boundary (what's present /
absent), dcc **extensions** beyond C89, the supported `printf`/`scanf`
conversions, and a few behavioural quirks.

> Inside the dcc repo, `dcc-c89-reference-guide.md` at the repo root is the
> exhaustive source; this file is the portable summary for use anywhere.
> (Header search and the silent-`<...>` gotcha are in SKILL.md.)

## Byte order

Multi-byte values are **little-endian** (Z80-native): `((unsigned char *)&n)[0]`
is the low byte — matters for `fread`/`fwrite` of binary records, `union`/`char *`
aliasing, and hand-built BDOS/FCB fields. (The runtime's internal `DE:HL` pairing
with `DE` = high word is a calling convention, not the memory layout.) Types are
in SKILL.md; `wchar_t` is 16-bit `unsigned int`.

## Implementation-defined constants

`BUFSIZ` = 256, `FOPEN_MAX` = 8, `RAND_MAX` = 32767 (the non-obvious ones; `EOF`
= -1, `SEEK_*` = 0/1/2, `EXIT_*`, `NULL` are standard). `EDOM` = 33, `ERANGE` =
34, plus CP/M file errnos (`ENOENT`, `EACCES`, `EMFILE`, `ENOSPC`, …).

## stdio.h

`FILE` is `int`; `stdin`/`stdout`/`stderr` exist. `fopen` modes: `"r"`/`"w"`/`"a"`
with optional `"+"`/`"b"`. The `v…` variants (`vprintf`/`vfprintf`/`vsprintf`)
share `printf`'s engine and conversion subset. Assume the rest of C89 stdio is
present *except* the absentees below.

**Not present (link error if called):** `fgetc`, `rename`, `tmpfile`, `tmpnam`,
`freopen`, `setvbuf`, `gets`, `ungetc`, `fgetpos`/`fsetpos`.

### printf-family conversions

Supported: `%d %i %u %x %X %c %s %%`, plus `%f` (**requires `-ffloatio`**) and the
`l` length modifier for 32-bit `long` (`%ld %li %lu %lx %lX`, **requires
`-flongio`**); `z` → `size_t` (16-bit, same as plain). Field width and `-`
left-justify work (`%-6d`, `%10s`); integer precision zero-fills (`%.4d`).
**Not supported:** `+`/space/`#` flags and `*` run-time width/precision — use
literal widths.

### scanf-family conversions

Supported: `%d %i %u %x %X %o %c %s %%` (`%i` auto-detects base from `0`/`0x`;
`%c` does **not** skip leading whitespace; `%s` stops at whitespace and is
NUL-terminated). Modifiers: field width (max input), `*` (suppress assignment),
`h` (no-op, `short`==`int`), `l` (store through `long *`). **Not supported:**
float input (`%f`/`%e`/`%g`), scansets (`%[...]`), `%n`, `%p`.

## stdlib.h

Standard C89 `malloc`/`free`/`qsort`/`bsearch`/`atoi`/`strtol`/… are present;
`RAND_MAX` is 32767. Quirk: the heap shares memory with the stack — **no guard**
(size with `-stack`; keep big buffers `static`/global).

**dcc extensions** (declared in `<stdlib.h>`, not C89):
- `inp(port)`/`outp(port,val)` — direct Z80 8-bit port I/O. `inp` runs
  `IN A,(port)` and returns the byte zero-extended to `int` (0..255); `outp` runs
  `OUT (port),A` with the low byte of `val`. Only the low 8 bits of `port` are
  used; the byte read back is device/emulator dependent. See `tportio.c`.
- `bdos(fn, dearg)` — calls the CP/M BDOS directly (`fn`→C, `dearg`→DE; byte
  result in the low byte). FCB/DMA-style calls return data through the memory
  `dearg` points at, not the return value. See `tbdos.c`/`crc.c`.
- `atof` returns `float` (single precision), not `double` — `strtod` is absent.

**Not present:** `abort`, `atexit`, `getenv`, `system`, the multibyte functions
(`mblen`/`mbtowc`/`wctomb`/…), `MB_CUR_MAX`, and `strtod`.

## Heap and stack sizing

The runtime publishes two 16-bit symbols (DCCRTL.MAC: `public __brk` /
`public __hlimit`). dcc prepends one underscore to C identifiers, so a C
`_brk` resolves to the asm label `__brk`. Declare and read them as `unsigned`:

```c
extern unsigned _brk;     /* current heap break: top of the allocated heap */
extern unsigned _hlimit;  /* heap ceiling = initial SP minus the reserved stack */

/* bytes malloc can still hand out by extending the heap */
static unsigned heap_free(void)
{
    if (_hlimit <= _brk)
        return 0;
    return _hlimit - _brk;
}
```

- `_brk` rises as `malloc`/`realloc` extend the heap and falls when the **top**
  block is freed (the runtime trims `__brk`). Blocks freed *below* `_brk` are
  reused by `malloc` but do **not** lower it, so `_hlimit - _brk` is a
  conservative "still-extendable" figure, not total free space.
- `_hlimit` is fixed once at startup to `initial_SP - stack_size`; it is the
  ceiling the heap must never cross.
- The heap base `__hbase` is **not** public, so total span isn't directly
  readable from C. If you want "bytes used", sample `_brk` once at startup and
  subtract later.
- **Stack size** is the compile-time `-stack N` value (default 512), baked in
  as the absolute symbol `__stack_size` — it is not a readable variable. The
  stack grows down from the top of the TPA and `_hlimit` already reserves `N`
  bytes for it, but there is **no guard**: a stack deeper than `N` bytes can
  still collide with heap data. Raise `-stack` for deeply recursive code or
  large automatic arrays.

`editc89.c` uses exactly this pattern (its `fremem()` shows a live free-memory
gauge).

## string.h

Full C89 `string.h`. dcc specifics: `strcoll` == `strcmp`; `strstr` is
case-sensitive (fold manually for a case-insensitive search); plus `strdup`
(dcc extension — `free` it later).

## ctype.h

Full C89 set **except** `isgraph` and `isblank`.

## math.h (single precision)

The standard C89 float math set is present as `…f` names (`sqrtf`, `powf`, `expf`,
`logf`, `sinf`, `cosf`, `tanf`, `atan2f`, `frexpf`, `modff`, …), each with an
unsuffixed alias (`sqrt`, `pow`, …) that stays single-precision, so portable
source compiles unchanged. `nextafterf` is a dcc extra.

Caveats: transcendentals are polynomial approximations (~**5–6 correct decimal
digits**), and `sin`/`cos`/`tan` range-reduction via `fmodf` degrades for very
large arguments. There is **no** `double`, `long double`, or `HUGE_VAL`, and
printing a float (`%f`) requires `-ffloatio`.


## setjmp.h / stdarg.h / stddef.h

Standard C89. dcc specifics: `jmp_buf` is 8 bytes (return address, SP, IX);
`va_list` is a `char *` walking the frame; `offsetof` is the builtin
`__offsetof` (supports nested members and constant indexes).

## unistd.h / fcntl.h (low-level CP/M file I/O)

`open`, `read`, `write`, `close`, `lseek`, `unlink`, `fsync`, `fdatasync`.
Flags from `<fcntl.h>`: `O_RDONLY` (0), `O_WRONLY` (1), `O_RDWR` (2),
`O_CREAT` (0100 octal), `O_TRUNC` (01000 octal). `off_t` is `long`.

## dirent.h (CP/M has no subdirectories — enumerates the drive)

`opendir`, `readdir`, `closedir` (macros over `dopn`/`drd`/`dcls`).
`struct dirent` has one member, `char d_name[13]` (8.3 name). See `cpmenumd.c`.

## errno.h / assert.h

Standard C89. dcc adds CP/M file errnos (`ENOENT`, `EACCES`, `EMFILE`, `ENOSPC`,
…) alongside `EDOM`/`ERANGE`.

## CP/M BDOS escape hatch

Declared in `<stdlib.h>` (a dcc/CP/M extension) for raw BDOS calls:

```c
int bdos(int fn, int dearg);   /* fn -> C, dearg -> DE; byte result in low byte */
```

FCB/DMA-style calls (directory/file ops) return their data through the memory
`dearg` points at, not the return value. Old CP/M C that declares its own
`extern int bdos();` still compiles — the K&R declaration is compatible with the
prototype. See `tbdos.c`, `crc.c` in the dcc repo.

## C89 standard headers that DO NOT exist in dcc

`<locale.h>`, `<signal.h>`, `<time.h>`. (`<stdbool.h>`/`<stdint.h>` are present
as C99-style conveniences but are not C89.)

## Pitfalls and worked examples

SKILL.md lists the deviations from standard C; this section adds symptom→fix
detail for the ones that need it. (Anything not covered behaves as standard C89.)

### Parsing a decimal string to `float`

`atof` is available in dcc as an extension (`#include <stdlib.h>`). It returns
`float` (IEEE 754 single precision) rather than `double` — C89 `atof` normally
returns `double`, but dcc has no `double` type. `strtod` is absent entirely.

### `%f` needs the `-ffloatio` build flag

Symptom: `printf("%f", x)` produces nothing useful. Float formatting is only
linked when you compile with `-f` / `-ffloatio`, so any program that prints
floats must be built with that flag.

### `%ld`/`%lu`/`%lx` need the `-flongio` build flag

Symptom: a `long` printed with `%ld`/`%li`/`%lu`/`%lx`/`%lX`/`%ls` comes out
wrong (or as a 16-bit value). The 32-bit `long` conversions are only linked when
you compile with `-fl` / `-flongio`; without it the `l` length modifier isn't
honored. Build with that flag whenever you print or `scanf` 32-bit values
(`dccmake dcc-flongio=true`, or `-fl` directly). This is independent of
`-ffloatio` — use both if a program prints both `long` and `float`.

### CP/M filename gotcha (beyond the 8.3 rule in SKILL.md)

ntvcm's filename mapper **asserts on any on-disk extension longer than 3
characters**, so delete stray `.DS_Store` files before directory-enumeration
tests or a full `runall.sh` (VS Code / file search may hide them).

### Stack-depth check

The stack/heap collision is unguarded (see SKILL.md). `spsmash.c` shows an
optional manual stack-depth check for deeply recursive code.

## Inlining rules (`static inline`)

`static inline` is the only inline form dcc acts on; plain `inline` (external
linkage) is **ignored** — parsed for source compatibility, but with no call-site
inlining and no C99 external-inline semantics, so it stays an ordinary
out-of-line function. What dcc can inline from a `static inline` helper:

- Simple return-expression helpers; scalar `int`/pointer/`long`/`float`
  expression helpers; simple struct/pointer member accessors.
- Early-return `if` chains lowered to conditional expressions, e.g.
  `if (tp >= tend) return 0; return tc[tp++];` or
  `if (k > 0) { n++; return 1; } return 0;` — folded into comma expressions
  rather than requiring a bare `return`.
- A guard `if` with no `return`/`else`, e.g. `if (sp <= 0) die("empty");` ahead
  of a later `return`, or as a standalone statement in a `void` body — the side
  effect runs conditionally, the surrounding code unconditionally.
- Statement-context `void` helpers made of expression statements such as
  `*dst = value`; their store expressions may contain ordinary helper calls such
  as `*dst = clamp((long)*dst + v)`. `void` helpers inline only when called as a
  statement.

Rules and fallbacks:

- `++`/`--` inside an inlined return expression is allowed only on operands that
  do not reach a parameter (globals are fine; incrementing a parameter verbatim
  would mutate the caller's argument once substituted).
- Hidden caller-frame temporaries preserve single evaluation for multi-use
  16-bit parameters such as `max(i++, j++)`. Multi-use `long`/`float` parameters
  with side-effecting arguments, inline bodies with local declarations, and
  unsupported statement bodies fall back to a normal call.
- When every call site inlines and the function's address is not taken, the
  private out-of-line copy is removed; if a call cannot be inlined safely, or the
  address is used, dcc keeps and calls that fallback body.

Size caveat: inlining a helper called from many sites (e.g. a bytecode VM's
per-opcode accessor used from a dozen `switch` cases) duplicates its body at each
call site. On CP/M's small fixed address space a `dcc-peep=false` (unoptimized)
binary can grow enough to shrink the heap and fail with an out-of-memory error
unrelated to the code's logic — the peephole-optimized build often stays small
enough to pass. Treat a no-peephole-only failure after adding `static inline` as
a size regression: compare `.COM` size with and without it, and prefer leaving a
many-call-site helper as a real function.
