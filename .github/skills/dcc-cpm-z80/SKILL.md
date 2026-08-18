---
name: dcc-cpm-z80
description: 'Write, build, test, and debug C for dcc targeting CP/M 2.2 on the Z80 (run under the ntvcm Altair 8800 emulator). Use for .c/.h sources compiled with dcc, or tasks mentioning dcc, dccmake, C89, C99, C11, CP/M, Z80, ntvcm, DCCRTL, or VT100/ANSI terminal apps. Treat dcc as a C89-base compiler with documented selected C99/C11 features and Z80/CP/M deviations: no double or long long, 32-bit float as the only floating type, 16-bit int/short/pointer/size_t, 32-bit long, signed char, and a subset library/runtime. Full feature and library inventories are in the reference files.'
argument-hint: 'Describe the dcc CP/M-Z80 task (write code, build, run under ntvcm, debug a failure)'
---

# dcc C for CP/M 2.2 / Z80

dcc is a cross-compiler (runs on the host) that emits Z80 assembly for CP/M 2.2.
The runtime is [DCCRTL.MAC](DCCRTL.MAC); programs run on real hardware or an
emulator such as **ntvcm** (Altair 8800).

**Assume standard C89 plus the selected C99/C11 features documented by dcc.**
dcc is not a hosted
desktop C implementation: the CP/M 2.2 runtime, Z80 data model, and DCCRTL
library subset are part of the compiler contract. Do not assume an unlisted
C99/C11 feature is supported; CP/M/Z80 limits always win over host ABI
expectations.

## Compiler conformance level

- C89 is the baseline; target-appropriate C99/C11 front-end features are
  supported where tested and documented below, and anything not yet implemented
  is a future candidate, not a permanent exclusion.
- Permanent target/runtime exceptions (the Z80/CP/M model wins over host ABI):
  `double`/`long double`, `long long`/64-bit integers, host ABI and
  host-sized-`int` assumptions, hosted byte-stream stdio, wide-character Unicode
  runtime behavior, POSIX, threads, and atomics. The C89 locale, signal, and time
  headers exist, but expose only CP/M-appropriate `C`-locale and unavailable/no-op
  service behavior where CP/M 2.2 has no corresponding facility.

## When to use

- Writing, porting, or reviewing C89-base code using dcc's documented C99/C11
  additions.
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
  `printf("%f", x)` consumes a 32-bit `float` directly. For a compile-time
  literal format, dcc detects `%f` at that call site and selects the float-capable
  runtime entry automatically.
- `<math.h>` provides the full single-precision set (`sinf`/`expf`/`powf`/… each
  with an unsuffixed alias that stays single-precision), but the transcendentals
  are ~5–6-digit polynomial approximations.
- `atof` and `strtod` return `float` rather than the standard `double` because
  dcc has no distinct double type.

**The library is a subset.** A missing function is a **link** error
(`unresolved external`), not a compile error, so check
[references/library.md](./references/library.md) before assuming one exists.
The shipped C89 headers include stdio, stdlib, locale, signal, and time surfaces,
with target-specific behavior documented in the reference; hosted POSIX and
Unicode facilities remain outside the runtime.

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

**`#pragma` support is selective.** Unknown pragmas are ignored for source
compatibility, so vendor-specific directives usually do not block a build. dcc
does give these pragmas target-specific behavior:

- `#pragma once` marks the current source/header as include-once. It is honored
  only in an active preprocessor branch, and later includes of the same
  canonical host path are skipped.
- `#pragma stack_check(on)` / `#pragma stack_check(off)` toggle stack-overflow
  guard emission from that point forward in the translation unit. They affect
  later function prologues and VLA allocations, not code already emitted.
- `#pragma push_macro("NAME")` / `#pragma pop_macro("NAME")` save and restore a
  macro definition state on dcc's macro stack.

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

C99 variadic macros and `__VA_ARGS__`, including empty variadic arguments, are
supported. C11 `_Static_assert` declarations are supported at file, block, and
`struct`/`union` member scope; `<assert.h>` defines `static_assert` as an alias.

Not implemented yet, but plausible front-end scope: GNU statement expressions,
`__builtin_expect`, and C11 `_Generic` for target-supported types.

Target-inapplicable or runtime-inapplicable exceptions: `double`/`long double`,
`long long`, 64-bit integer typedefs/operations, host ABI checks,
host-sized-int expectations, hosted byte-stream stdio behavior, wide-character
Unicode library behavior, POSIX services, C11 threads, and C11 atomics. CP/M has
no asynchronous signals, locale database, processor clock, or real-time clock;
the corresponding C89 APIs therefore return documented stub/default results.

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
4. `m80c` / `L80` — `dccmake` assembles the app `.MAC`s plus `RTLMIN.MAC` with
  the native host `m80c` by default, then runs Microsoft's `L80` under `ntvcm`
  to link the final `.COM`. Set `dcc-use-emulated-m80=true` to assemble with
  Microsoft's `M80` under `ntvcm` instead.

**Build/run one program** (compile → peephole → m80c → strip runtime → m80c → L80):

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

Literal `printf`-family format strings do not need `dcc-floatio=true` or
`dcc-flongio=true`: dcc selects float and long runtime variants independently at
each call site. A non-literal format string conservatively selects both. These
settings remain available as blanket force-on overrides; the corresponding
`dcc-no-floatio=true` / `dcc-no-longio=true` settings force support off and must
only be used when no affected conversion can reach any call site.

For repeatable local builds, put the same `dcc-*` settings (one per line) in a
`dccmake.txt` in the working directory; command-line settings override it.

> The source and output names used by `dccmake` must be 8.3-clean (base ≤ 8
> chars, extension ≤ 3, no extra dots). ntvcm reports
> `argument is not a valid CP/M 8.3 filename` for a non-conforming name —
> rename the file when you see it.

**Useful `dcc` options:** `-o file` (output .mac), `-c`/`-module` (linkable
module), `-f`/`-ffloatio` (force `%f` support on every `printf`-family call),
`-fl`/`-flongio` (force 32-bit `long` formats on every `printf`-family call),
`-fno-floatio`/`-fno-longio` (force those paths off), `-fstack-check` (abort on
stack overflow), `-stack N`/`-s N`/`--stack N` (reserve
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

Notes: when `dcc-use-emulated-m80=true`, M80 needs CRLF (`dccmake` handles this).
`RTLMIN.MAC` is generated per-app by `dccrtlstrip` during the build — don't
hand-edit it.

## Top pitfalls

The deviations above are the pitfalls. For worked examples (the `float` decimal
parser, formatted-I/O auto-detection and overrides, 16-bit overflow, signed
`char`, CP/M 8.3 names, the
stack/heap collision, and supported pragmas), the full inlining rules, and the
function inventory and `printf`/`scanf` conversion tables, see
[references/library.md](./references/library.md).

## Workflow

1. **Plan for the deviations.** Floating point → single precision (no `double`);
  decimal parsing → dcc's `float`-returning `atof`, or a small parser if you
  need different semantics; `time`/`signal`/`locale` → don't exist.
2. **Keep pragma assumptions narrow.** `#pragma once`, `#pragma stack_check`,
  and `#pragma push_macro`/`#pragma pop_macro` are supported; unknown pragmas
  are ignored, not diagnosed.
3. **Check the library** in [references/library.md](./references/library.md)
   before calling anything unverified — a missing function is a link error,
   not a compile error.
4. **Match repo conventions.** Read a nearby working program first. In the dcc
   repo, the exhaustive reference is
   [dcc-c89-reference-guide.md](dcc-c89-reference-guide.md) at the repo root.
5. **Build and run**: `dccmake app.c dcc-output=APP dcc-peep=true && ntvcm build/APP.COM`;
  literal `%f` and long formats are detected automatically. Redirect stdin for
  interactive apps and compare against expected output.
