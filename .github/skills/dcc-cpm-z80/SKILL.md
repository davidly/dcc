---
name: dcc-cpm-z80
description: 'Write, build, test, and debug C89/C99/C11-targeted code for the dcc compiler targeting CP/M 2.2 on the Z80 (run under the ntvcm Altair 8800 emulator). Use for .c/.h sources compiled with dcc, or tasks mentioning dcc, C89, C99, C11, CP/M, CP/M 2.2, Z80, ntvcm, DCCRTL, ma.sh, or VT100/ANSI CP/M terminal apps. Treat dcc as standard C89 plus a first-class _Bool scalar type and target-appropriate C99/C11 front-end compatibility EXCEPT for the Z80/CP/M deviations this skill documents: no double or long long, 32-bit float as the only floating type, 16-bit int/short/pointer/size_t, 32-bit long, signed char, and a subset library/runtime. Full library/printf/scanf inventory and pitfalls are in the reference files.'
argument-hint: 'Describe the C89/C99/C11 CP/M-Z80 task (write code, build, run under ntvcm, debug a failure)'
---

# dcc C for CP/M 2.2 / Z80

dcc is a cross-compiler (runs on the host) that emits Z80 assembly for CP/M 2.2.
The runtime is [DCCRTL.MAC](DCCRTL.MAC); programs run on real hardware or an
emulator such as **ntvcm** (Altair 8800).

**Assume standard C89 plus dcc's first-class `_Bool` scalar type and
target-appropriate C99/C11 front-end compatibility.** dcc is not a hosted
desktop C implementation: the CP/M 2.2 runtime, Z80 data model, and DCCRTL
library subset are part of the compiler contract. Anything not listed here
should be treated as ordinary C89/C99/C11, but CP/M/Z80 limits always win over
host ABI expectations.

## Compiler conformance level

- C89 is the baseline language, except where the Z80 data model makes a hosted
  assumption impossible (`double`, `long long`, host-sized `int`, and host ABI
  macros are not part of the target contract).
- Target-appropriate C99/C11 front-end compatibility is supported when tested and
  documented below.
- Not-yet-implemented C99/C11/GNU front-end features are candidates for future
  support, not permanent target exclusions.
- True target/runtime exceptions are: `double`/`long double`, `long long`/64-bit
  integers, host ABI assumptions, host-sized integer expectations, hosted
  byte-stream stdio behavior, wide-character Unicode runtime behavior, POSIX,
  locale, signal, time, threads, and atomics.

## When to use

- Writing, porting, or reviewing C89/C99/C11 code compiled by `dcc`.
- Building/running/debugging a dcc program (`ma.sh`, `ntvcm`).
- CP/M file I/O, VT100/ANSI console UIs, or DCCRTL work.

## Deviations from standard C

**Types — a 16-bit machine:**

| Type | dcc | Note |
| ---- | --- | ---- |
| `int` / `short` | 16-bit | overflow at ±32767; use `long` + `%ld` for range |
| `long` | 32-bit | |
| `float` | 32-bit | **the only floating type** |
| `double` / `long double` | — | **not supported as a distinct type; use `float`** |
| `_Bool` / `bool` | 8-bit | First-class scalar type; `stdbool.h` aliases `bool` to `_Bool`; nonzero `_Bool` stores/casts/initializers/parameter loads/returns normalize to `1` |
| pointer / `size_t` / `ptrdiff_t` / `wchar_t` | 16-bit | flat 64 KB space |
| `char` | 8-bit **signed** | use `unsigned char` for bytes ≥ 0x80 / table indices |
| `FILE` | `int` | |

Multi-byte values are little-endian (Z80-native).

**Floating point is single-precision only:**

- Write `float`; unsuffixed constants (`3.14`) are already `float`, not `double`.
- No `float`→`double` promotion in varargs (there is no double), so
  `printf("%f", x)` consumes a 32-bit `float` directly — but **requires the
  `-ffloatio` build flag**; without it `%f` silently does nothing.
- `<math.h>` provides the full single-precision set (`sinf`/`expf`/`powf`/… each
  with an unsuffixed alias that stays single-precision), but the transcendentals
  are ~5–6-digit polynomial approximations.
- `atof` is a dcc extension returning `float` (not `double`); `strtod` is absent.

**The library is a subset.** A missing function is a **link** error
(`unresolved external`), not a compile error, so check
[references/library.md](./references/library.md) before assuming one exists.
Notably absent: `strtod`, `<locale.h>`/`<signal.h>`/`<time.h>`, and
some stdio entries (`fgetc`, `ungetc`, `rename`, …).

**printf/scanf are a subset.** No `+`/space/`#` flags and no `*`
width/precision; scanf is integer/string only (no `%f`, scansets, `%n`, `%p`).
Conversion tables in library.md.

**No stack/heap guard.** Heap and stack share memory and can collide silently.
Size the stack with `-stack N` (default 512); keep big buffers `static`/global.

**Source filenames MUST be 8.3 and uppercase-safe** (≤ 8-char base, ≤ 3-char
extension, no extra dots). `foo.c` → `FOO.COM`, run as `ntvcm FOO`. A source
whose name violates 8.3 (e.g. `my_long_name.c`, `parse.test.c`) won't build —
ntvcm reports `argument is not a valid CP/M 8.3 filename`; rename the file when
you see that error.

**Missing `<...>` headers are silently ignored** — calls fall back to implicit
`int` and still link via the runtime, with no type-checking. A missing
`"..."` header is fatal. If standard calls compile but misbehave, check that
`-I` actually resolves the dcc headers.

## C99/C11 front-end compatibility dcc accepts (beyond C89)

These behave as standard C99: `for`-init declarations with loop scope, `//` line
comments, and block-scoped declarations (inner blocks shadow outer names).
`const`/`volatile`/`register`/`auto` are accepted but mostly inert (`const`
constant-folds initializers only — not read-only memory).
K&R function definitions are still accepted; prefer prototypes for new code.

`static inline` is the supported inline form for small helper functions. dcc can
inline simple return-expression helpers, early-return `if` chains lowered to
conditional expressions, simple struct/pointer member accessors,
statement-context `void` helpers made of one or more expression statements such
as `*dst = value`, and scalar
`int`/pointer/`long`/`float` expression helpers. `void` helpers inline only when
called as a statement; their assignment/store expressions may contain ordinary
helper calls such as `*dst = clamp((long)*dst + v)`. When every call site inlines
and the function address is not taken,
the private out-of-line static helper body is removed; if a call cannot be
inlined safely, or if the function address is used, dcc keeps and calls that
private fallback body. Hidden
caller-frame temporaries preserve single evaluation for multi-use 16-bit
parameters such as `max(i++, j++)`; multi-use `long`/`float` parameters with
side-effecting arguments, inline bodies with local declarations, and unsupported
statement bodies fall back. Plain externally linked `inline` is parsed
for source compatibility but does not yet have C99 external-inline linkage
semantics or call-site inlining.

dcc has a first-class C99-style `_Bool` scalar type: it is 1 byte wide, and
nonzero values normalize to `1` on `_Bool` stores, casts, initializers,
parameter loads, and returns. Include `stdbool.h` for the portable spellings
`bool`, `true`, and `false`. dcc also accepts practical front-end compatibility
used by common C99-era code: forward enum declarations are parsed as `int`-sized
enum types, including inside function prototypes and function-pointer
declarators such as `int (*member)(enum E value)`. C11 anonymous struct and
union members are accepted; members of anonymous aggregates are promoted for
ordinary member access, including nested forms, and aggregate initialization
through anonymous struct/union members is supported. GNU
`__attribute__((...))` annotations are skipped when they appear in supported
declaration positions.

Not implemented yet, but plausible front-end scope: C99 designated initializers,
C99 array designators, C99 compound literals, C99 variadic macros, GNU statement
expressions, `__builtin_expect`, and C11 `_Generic` for target-supported types.

Target-inapplicable or runtime-inapplicable exceptions: `double`/`long double`,
`long long`, 64-bit integer typedefs/operations, host ABI checks,
host-sized-int expectations, hosted byte-stream stdio behavior, wide-character
Unicode library behavior, POSIX services, locale, signal, time, C11 threads, and
C11 atomics.

Automatic one-dimensional VLAs with a simple identifier bound, such as
`char buf[n]`, are supported by reserving stack space at runtime. Keep them
small: heap and stack still share the CP/M transient program area and have no
guard beyond explicit stack checking.

**Identifiers:** full internal significance; externals stay distinct well past
C89's 6-char minimum (verified to ~13 chars), and only ~16+ identical leading
characters can silently collide at link time — make such a one-file helper
`static` if it ever matters. (This is *not* BDS C's 7-char rule.)

## Build and run

The standard helper scripts live in the dcc repo. **Set these env vars first**
(they make `ma.sh` use the local binaries):

```sh
export PATH="/Users/<USER_NAME_FOLDER>/GitHub/ntvcm:/Users/<USER_NAME_FOLDER>/GitHub/dcc:$PATH"
export DCC=./dcc DCCPEEP=./dccpeep DCCRTLSTRIP=./dccrtlstrip
```

**Build/run one program** (compile → peephole → strip runtime → M80 → L80):

```sh
./ma.sh foo peep      # foo.c -> FOO.COM (peephole optimised); use 'nopeep' to skip
ntvcm FOO             # run it (uppercase, no extension)
ntvcm FOO ARG1 ARG2   # with CP/M command-line args
```

> The source name passed to `ma.sh` must be 8.3-clean (base ≤ 8 chars,
> extension ≤ 3, no extra dots). ntvcm reports
> `argument is not a valid CP/M 8.3 filename` for a non-conforming name —
> rename the file when you see it.

**Useful `dcc` options:** `-o file` (output .mac), `-c`/`-module` (linkable
module), `-f`/`-ffloatio` (float printf), `-stack N`/`-s N`/`--stack N` (reserve
stack; default 512 — heap and stack share memory, **no guard**), `-I dir` (or
joined `-Idir`; repeatable), `-Dname[=v]`,
`-Uname`, `-v`, `-h`. `_DCC_=1` is always predefined.

**Finding the standard headers (`-I`).** dcc resolves `#include <stdio.h>` by
checking the current directory first, then each `-I` directory in order. The
bundled headers (`stdio.h`, `stdlib.h`, `string.h`, `math.h`, …) live in the
**dcc repo root**, so:

- Building **inside** the dcc repo (as `ma.sh` does): they're found
  automatically via the current directory — no `-I` needed.
- Building **elsewhere**: point dcc at the repo, e.g.
  `dcc -I /path/to/dcc myapp.c -o myapp.mac` (repeat `-I` for more dirs).

Gotcha: a `<...>` header that isn't found is **silently ignored** (you lose its
prototypes and fall back to implicit `int`, so calls still compile and link via
the runtime but without type-checking); a missing `"..."` header is a fatal
error. If standard calls compile yet misbehave, check that `-I` actually
resolves the dcc headers.

Notes: M80 needs CRLF (`ma.sh` converts). `RTLMIN.MAC` is generated per-app by
`dccrtlstrip` during the build — don't hand-edit it.

## Top pitfalls

The deviations above are the pitfalls. For worked examples (the `float` decimal
parser, `%f`/`-ffloatio`, 16-bit overflow, signed `char`, CP/M 8.3 names, the
stack/heap collision) see [references/pitfalls.md](./references/pitfalls.md);
for the full function inventory and `printf`/`scanf` conversion tables see
[references/library.md](./references/library.md).

## Workflow

1. **Plan for the deviations.** Floating point → single precision (no `double`);
   decimal parsing → a `float` parser (no `atof`); `time`/`signal`/`locale` →
   don't exist.
2. **Check the library** in [references/library.md](./references/library.md)
   before calling anything unverified — a missing function is a link error,
   not a compile error.
3. **Match repo conventions.** Read a nearby working program first. In the dcc
   repo, the exhaustive reference is
   [dcc-c89-reference-guide.md](dcc-c89-reference-guide.md) at the repo root.
4. **Build and run**: `./ma.sh <name> peep && ntvcm <NAME>` (add the `-ffloatio`
   path if you use `%f`); redirect stdin for interactive apps and compare
   against expected output.
