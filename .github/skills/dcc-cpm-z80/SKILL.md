---
name: dcc-cpm-z80
description: 'Write, build, test, and debug C89/C99/C11-targeted code for the dcc compiler targeting CP/M 2.2 on the Z80 (run under the ntvcm Altair 8800 emulator). Use for .c/.h sources compiled with dcc, or tasks mentioning dcc, dccmake, C89, C99, C11, CP/M, CP/M 2.2, Z80, ntvcm, DCCRTL, or VT100/ANSI CP/M terminal apps. Treat dcc as standard C89 plus a first-class _Bool scalar type and target-appropriate C99/C11 front-end compatibility EXCEPT for the Z80/CP/M deviations this skill documents: no double or long long, 32-bit float as the only floating type, 16-bit int/short/pointer/size_t, 32-bit long, signed char, and a subset library/runtime. Full library/printf/scanf inventory and pitfalls are in the reference files.'
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

- C89 is the baseline; target-appropriate C99/C11 front-end features are
  supported where tested and documented below, and anything not yet implemented
  is a future candidate, not a permanent exclusion.
- Permanent target/runtime exceptions (the Z80/CP/M model wins over host ABI):
  `double`/`long double`, `long long`/64-bit integers, host ABI and
  host-sized-`int` assumptions, hosted byte-stream stdio, wide-character Unicode
  runtime behavior, POSIX, locale, signal, time, threads, and atomics.

## When to use

- Writing, porting, or reviewing C89/C99/C11 code compiled by `dcc`.
- Building/running/debugging a dcc program (`dccmake`, `ntvcm`).
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

**Inlining — only `static inline`.** `static inline` is the *only* inline form
dcc acts on; plain `inline` (external linkage) is **ignored** (parsed for source
compatibility, but stays an ordinary out-of-line function). dcc inlines small
helpers and drops the out-of-line copy when every call site inlines and the
address isn't taken; an inlined body must not mutate a *parameter* (globals are
fine). Size caveat: many-call-site inlining bloats the `.COM`, and a
`dcc-peep=false` build can grow enough to starve the heap (an out-of-memory
failure unrelated to logic) — prefer a real function there. Full rules:
[references/library.md](./references/library.md).

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

C99 designated initializers for struct and array members are supported,
including out-of-order, nested, and array-index (`[k] = v`) designators, in both
file-scope and block-scope objects. Compound literals are supported for
file-scope constant initializers and for address-taken block-scope literals
(`&(struct T){ ... }`); full block-scope compound-literal value/copy semantics
are only partly supported. GNU range designators (`[0 ... 3]`) are not supported.

Not implemented yet, but plausible front-end scope: C99 variadic macros, GNU
statement expressions, `__builtin_expect`, and C11 `_Generic` for
target-supported types.

Target-inapplicable or runtime-inapplicable exceptions: `double`/`long double`,
`long long`, 64-bit integer typedefs/operations, host ABI checks,
host-sized-int expectations, hosted byte-stream stdio behavior, wide-character
Unicode library behavior, POSIX services, locale, signal, time, C11 threads, and
C11 atomics.

Automatic VLAs (local arrays whose **outermost** dimension is a run-time value)
are supported by reserving stack space when the declaration is reached, e.g.
`char buf[n]` or `int grid[n][3]` (inner dimensions must be compile-time
constants). The storage is released at block-scope exit — including each loop
iteration, `break`, `continue`, `return`, and a `goto` that leaves the scope
(forward or backward, across nested VLA scopes). `sizeof` applied to a **whole**
VLA is supported for this subset and yields the run-time byte size (`sizeof a[0]`
times the element count still works too). Rejected, never miscompiled:
a variable **inner** dimension (`a[n][m]`), a whole-VLA `sizeof` in a
constant-expression context (array bound, `case`, `enum`, `#if`), and jumping
**into** a VLA scope via `goto`, `case`, or `default`. Keep them small: heap and
stack still share the CP/M transient program area and have no guard beyond
explicit stack checking (`-fstack-check` bounds-checks each VLA allocation).

**Identifiers:** full internal significance; externals stay distinct well past
C89's 6-char minimum (verified to ~13 chars), and only ~16+ identical leading
characters can silently collide at link time — make such a one-file helper
`static` if it ever matters. (This is *not* BDS C's 7-char rule.)

## Build and run

The standard build helper is `dccmake`, which runs the full CP/M pipeline and
uses the local tools on `PATH` by default. If needed, put the dcc and ntvcm
directories first on `PATH`:

```sh
export PATH="/Users/<USER_NAME_FOLDER>/GitHub/ntvcm:/Users/<USER_NAME_FOLDER>/GitHub/dcc:$PATH"
```

**The pipeline — what each tool does.** `dccmake` orchestrates the whole CP/M
build so you rarely call the stages directly (get each tool's own flags with
`-h`, e.g. `dcc -h`, `dccpeep -h`, `dccrtlstrip -h`, `dccmake -h`):

1. `dcc` — the C front end: compiles each `.c` to Z80 `.MAC` assembly
   (`dcc app.c -o APP.MAC`); inputs after the first are compiled with `-module`.
2. `dccpeep` — the peephole optimizer: rewrites `.MAC` → `.MAC` for smaller,
   faster code. `dccmake dcc-peep=true` (the default) runs it; `dcc-peep=false`
   skips it, giving a larger/slower `.COM`. dccpeep itself has `-Ot` (time,
   default) and `-Os` (size) modes, but `dccmake` always uses the default `-Ot`
   — choose `-Os` only by running `dccpeep` by hand. `dcc-allow-undocumented-z80=true`
   forwards `-fundocumented-z80`.
3. `dccrtlstrip` — the runtime stripper: reads the full runtime `DCCRTL.MAC` and
   writes a per-app `RTLMIN.MAC` with only the routines your program references,
   so the `.COM` isn't padded with the whole libc. It regenerates every build —
   don't hand-edit `RTLMIN.MAC`.
4. `M80` / `L80` — Microsoft's assembler and linker (run under `ntvcm`):
   assemble the app `.MAC`s plus `RTLMIN.MAC`, then link the final `.COM`.

**Build/run one program** (compile → peephole → strip runtime → M80 → L80):

```sh
dccmake foo.c dcc-output=FOO dcc-peep=true   # foo.c -> build/FOO.COM
ntvcm build/FOO.COM                          # run it
ntvcm build/FOO.COM ARG1 ARG2                # with CP/M command-line args
```

Use `dcc-peep=false` for an unoptimized build. `dccmake` also accepts other
settings, e.g.:

```sh
dccmake foo.c bar.c dcc-output=FOO dcc-stack-bytes=768 dcc-floatio=true
dccmake foo.c dcc-output=FOO dcc-include-directory=include dcc-define=DEBUG=1
```

For repeatable local builds, put the same `dcc-*` settings (one per line) in a
`dccmake.txt` in the working directory; command-line settings override it.

> The source and output names used by `dccmake` must be 8.3-clean (base ≤ 8
> chars, extension ≤ 3, no extra dots). ntvcm reports
> `argument is not a valid CP/M 8.3 filename` for a non-conforming name —
> rename the file when you see it.

**Useful `dcc` options:** `-o file` (output .mac), `-c`/`-module` (linkable
module), `-f`/`-ffloatio` (float printf), `-fl`/`-flongio` (32-bit `long`
printf/scanf, e.g. `%ld`/`%lu`/`%lx`), `-fstack-check` (abort on stack
overflow), `-stack N`/`-s N`/`--stack N` (reserve
stack; default 512 — heap and stack share memory, **no guard**), `-I dir` (or
joined `-Idir`; repeatable), `-Dname[=v]`,
`-Uname`, `-v`, `-h`. `_DCC_=1` is always predefined.

**Finding the standard headers (`-I`).** dcc resolves `#include <stdio.h>` by
checking the current directory first, then each `-I` directory in order. The
bundled headers (`stdio.h`, `stdlib.h`, `string.h`, `math.h`, …) live in the
**dcc repo root**, so:

- Building **inside** the dcc repo: they're found
  automatically via the current directory — no `-I` needed.
- Building **elsewhere**: point dcc at the repo, e.g.
  `dcc -I /path/to/dcc myapp.c -o myapp.mac` (repeat `-I` for more dirs).

Gotcha (see Deviations): an unfound `<...>` header is **silently ignored**
(implicit `int`, no type-checking); an unfound `"..."` header is fatal — so if
standard calls compile yet misbehave, confirm `-I` resolves the dcc headers.

Notes: M80 needs CRLF (`dccmake` handles this). `RTLMIN.MAC` is generated per-app by
`dccrtlstrip` during the build — don't hand-edit it.

## Top pitfalls

The deviations above are the pitfalls. For worked examples (the `float` decimal
parser, `%f`/`-ffloatio`, 16-bit overflow, signed `char`, CP/M 8.3 names, the
stack/heap collision), the full inlining rules, and the function inventory and
`printf`/`scanf` conversion tables, see
[references/library.md](./references/library.md).

## Workflow

1. **Plan for the deviations.** Floating point → single precision (no `double`);
  decimal parsing → dcc's `float`-returning `atof`, or a small parser if you
  need different semantics; `time`/`signal`/`locale` → don't exist.
2. **Check the library** in [references/library.md](./references/library.md)
   before calling anything unverified — a missing function is a link error,
   not a compile error.
3. **Match repo conventions.** Read a nearby working program first. In the dcc
   repo, the exhaustive reference is
   [dcc-c89-reference-guide.md](dcc-c89-reference-guide.md) at the repo root.
4. **Build and run**: `dccmake app.c dcc-output=APP dcc-peep=true && ntvcm build/APP.COM`
  (set `dcc-floatio=true` if you use `%f`); redirect stdin for interactive apps
  and compare against expected output.
