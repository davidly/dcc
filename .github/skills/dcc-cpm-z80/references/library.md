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

> **Where the headers live (`-I`).** dcc resolves `#include <stdio.h>` from the
> current directory first, then each `-I` directory in order. The bundled
> headers sit in the **dcc repo root**, so building inside the repo needs no
> `-I`; building elsewhere needs `dcc -I /path/to/dcc …` (both `-I dir` and the
> joined `-Idir` form work; repeat for more dirs). A `<...>` header that isn't
> found is **silently ignored** (calls fall back to implicit `int`), whereas a
> missing `"..."` header is fatal.

## Type sizes and byte order

The full type table is in SKILL.md (16-bit `int`/ptr/`size_t`, 32-bit `long`,
32-bit `float` as the only floating type, signed `char`, `FILE` is `int`).
`wchar_t` is 16-bit (`unsigned int`, for `L"..."`).

Multi-byte values are **little-endian** (Z80-native): `((unsigned char *)&n)[0]`
is the low byte. Matters for `fread`/`fwrite` of binary records, `union`/`char *`
aliasing, and hand-built BDOS/FCB fields. (The runtime's internal `DE:HL`
pairing with `DE` = high word is a calling convention, not the memory layout.)

## Useful constants

- `<stdio.h>`: `EOF` = -1, `BUFSIZ` = 256, `SEEK_SET`/`SEEK_CUR`/`SEEK_END` = 0/1/2, `FOPEN_MAX` = 8.
- `<stdlib.h>`: `EXIT_SUCCESS` = 0, `EXIT_FAILURE` = 1, `RAND_MAX` = 32767, `NULL` = 0.
- `<errno.h>`: `EDOM` = 33, `ERANGE` = 34 (plus CP/M file errnos: `ENOENT`, `EACCES`, `EMFILE`, `ENOSPC`, …).

## stdio.h

**Declared in `<stdio.h>` and implemented:** `printf`, `fprintf`, `sprintf`,
`vprintf`, `vfprintf`, `vsprintf`, `putchar`, `putc`, `fputc`, `puts`, `fputs`,
`getchar`, `getc`, `perror`, `scanf`, `fscanf`, `sscanf`, `fopen`, `fclose`,
`fflush`, `fgets`, `fread`, `fwrite`, `fseek`, `ftell`, `rewind`, `feof`,
`ferror`, `clearerr`, `setbuf`, `remove`. Streams `stdin`, `stdout`, `stderr`
exist; `FILE` is `int`.

`fopen` modes: `"r"`, `"w"`, `"a"`, with optional `"+"`/`"b"`.

The `v…` variants take a `va_list` (from `<stdarg.h>`) and share the same
formatting engine and conversion subset as `printf`, so you can write your own
`printf`-style wrappers. (`<stdio.h>` includes `<stdarg.h>` for `va_list`.)

**Not present (link error if called):** `fgetc`, `rename`, `tmpfile`, `tmpnam`,
`freopen`, `setvbuf`, `gets`, `ungetc`, `fgetpos`/`fsetpos`.

### printf-family conversions

| Spec       | Meaning                                              |
| ---------- | ---------------------------------------------------- |
| `%d`, `%i` | signed 16-bit decimal                                |
| `%u`       | unsigned 16-bit decimal                              |
| `%x`, `%X` | unsigned 16-bit hex (lower/upper)                    |
| `%c`       | single character                                     |
| `%s`       | NUL-terminated string                                |
| `%f`       | 32-bit float — **requires `-ffloatio`**              |
| `%%`       | literal percent                                      |

- Length modifiers: `l` → 32-bit `long` (`%ld %li %lu %lx %lX`); `z` → `size_t`
  (16-bit, so same as plain).
- Field width (`%6d`, `%10s`) left-pads with spaces; `-` flag left-justifies
  (`%-6d`); precision on integers zero-fills (`%.4d`).
- **Not supported:** `+`, space, and `#` flags; `*` run-time width/precision.
  Use literal widths.

### scanf-family conversions

| Spec       | Meaning                                              |
| ---------- | ---------------------------------------------------- |
| `%d`       | signed decimal                                       |
| `%i`       | signed, base auto-detected (`0`, `0x`)               |
| `%u`       | unsigned decimal                                     |
| `%x`, `%X` | unsigned hex (optional `0x`)                         |
| `%o`       | unsigned octal                                       |
| `%c`       | character (does **not** skip leading whitespace)     |
| `%s`       | non-whitespace string (runtime NUL-terminates)       |
| `%%`       | literal percent                                      |

- Modifiers: field width (max input), `*` (suppress assignment), `h` (no effect,
  `short`==`int`), `l` (store through `long *`).
- **Not supported:** float input (`%f`/`%e`/`%g`), scansets (`%[...]`), `%n`, `%p`.

## stdlib.h

**Implemented:** `malloc`, `calloc`, `realloc`, `free`, `atoi`, `atol`,
`strtol`, `strtoul`, `abs`, `labs`, `div`, `ldiv`, `qsort`, `bsearch`, `exit`,
`rand`, `srand`. `div_t`/`ldiv_t`, `EXIT_SUCCESS`/`EXIT_FAILURE`, `RAND_MAX`
(32767).

- These behave per standard C89 (`malloc`/`realloc`/`free`, `qsort`/`bsearch`
  with `int cmp(const void *, const void *)`, `atoi`/`atol`, `strtol`/`strtoul`
  full base-2..36 conversions). Quirk: heap shares memory with the stack —
  **no stack/heap guard** (size with `-stack`; keep big buffers `static`/global).
- `inp(port)`/`outp(port,val)` are a **dcc extension** (declared in
  `<stdlib.h>`, not C89): direct Z80 8-bit port I/O. `inp` runs `IN A,(port)`
  and returns the byte zero-extended to `int` (0..255); `outp` runs
  `OUT (port),A`, sending the low byte of `val`. Only the low 8 bits of `port`
  are used; the byte read back is device/emulator dependent. See `tportio.c`.
- `bdos(fn, dearg)` is a **dcc/CP/M extension** (declared in `<stdlib.h>`, not
  C89): calls the CP/M BDOS entry point directly (`fn` -> C, `dearg` -> DE; the
  byte result is in the low byte). FCB/DMA-style calls return their data through
  the memory `dearg` points at, not the return value. See the escape-hatch note
  below and `tbdos.c`/`crc.c`.

**Not present** (neither declared in `<stdlib.h>` nor in the runtime):
`abort`, `atexit`, `getenv`, `system`, the multibyte
functions (`mblen`/`mbtowc`/`wctomb`/…), and `MB_CUR_MAX`. `strtod` is absent
(returns `double`, which dcc doesn't have). `atof` is available as a dcc
extension declared as `float atof(const char *)` — it returns IEEE 754 single
precision rather than `double`.

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

**All implemented:** `memcpy`, `memmove`, `memset`, `memcmp`, `memchr`,
`strlen`, `strcpy`, `strncpy`, `strcat`, `strncat`, `strcmp`, `strncmp`,
`strcoll` (== `strcmp`), `strchr`, `strrchr`, `strstr`, `strspn`, `strcspn`,
`strpbrk`, `strtok`, `strerror`. Plus `strdup` (a dcc extension; `free` it later).

`strstr` is case-sensitive; for case-insensitive search write a small ASCII-fold
helper (see editor `strifnd` pattern in the dcc repo).

## ctype.h

`isalpha`, `isdigit`, `isalnum`, `isspace`, `isupper`, `islower`, `isxdigit`,
`isprint`, `iscntrl`, `ispunct`, `toupper`, `tolower`. (No `isgraph`/`isblank`.)

## math.h (single precision)

`<math.h>` provides single-precision (`…f`) functions, each with an unsuffixed
C89 macro alias so portable source compiles unchanged (the operation stays
single-precision):

- **Rounding/remainder/roots:** `fabsf`, `floorf`, `ceilf`, `sqrtf`, `fmodf`,
  `nextafterf`.
- **Exponential/log:** `expf`, `logf`, `log10f`, `powf`.
- **Trigonometric:** `sinf`, `cosf`, `tanf`, `asinf`, `acosf`, `atanf`, `atan2f`.
- **Hyperbolic:** `sinhf`, `coshf`, `tanhf`.
- **Decomposition:** `frexpf`, `ldexpf`, `modff`.

Unsuffixed aliases: `fabs`, `floor`, `ceil`, `sqrt`, `fmod`, `exp`, `log`,
`log10`, `pow`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`,
`cosh`, `tanh`, `frexp`, `ldexp`, `modf`.

The transcendentals are single-precision polynomial approximations: expect about
**5–6 correct decimal digits**, not full `float` round-trip accuracy. The
range-reduction in `sinf`/`cosf`/`tanf` uses `fmodf`, so accuracy degrades for
very large arguments. There is **no** `double`, no `long double`, and no
`HUGE_VAL`. Printing a float (`%f`) still requires the `-ffloatio` build flag.

`strtod` is absent (returns `double`); use `strtol`/`strtoul` for integers.
`atof` is available as a dcc extension returning `float` (single precision).


## setjmp.h / stdarg.h / stddef.h

Standard C89 (`setjmp`/`longjmp`, `va_start`/`va_arg`/`va_end`, `size_t`/
`ptrdiff_t`/`wchar_t`/`NULL`/`offsetof`). dcc specifics: `jmp_buf` is 8 bytes
(return address, SP, IX); `va_list` is a `char *` walking the frame; `offsetof`
is the builtin `__offsetof` (supports nested members and constant indexes).

## unistd.h / fcntl.h (low-level CP/M file I/O)

`open`, `read`, `write`, `close`, `lseek`, `unlink`, `fsync`, `fdatasync`.
Flags from `<fcntl.h>`: `O_RDONLY` (0), `O_WRONLY` (1), `O_RDWR` (2),
`O_CREAT` (0100 octal), `O_TRUNC` (01000 octal). `off_t` is `long`.

## dirent.h (CP/M has no subdirectories — enumerates the drive)

`opendir`, `readdir`, `closedir` (macros over `dopn`/`drd`/`dcls`).
`struct dirent` has one member, `char d_name[13]` (8.3 name). See `cpmenumd.c`.

## errno.h / assert.h

Standard C89: `extern int errno;`, `assert(expr)` (compile out with `NDEBUG`).
File errnos: `ENOENT`, `EACCES`, `EMFILE`, `ENOSPC`, … (plus `EDOM`/`ERANGE`).

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
