# dcc C89 pitfalls — worked examples

SKILL.md lists the deviations from standard C; this file adds symptom→fix
detail and code for the ones that need it. (Anything not covered behaves as
standard C89.)

## Parsing a decimal string to `float`

`atof` is available in dcc as an extension (`#include <stdlib.h>`). It returns
`float` (IEEE 754 single precision) rather than `double` — C89 `atof` normally
returns `double`, but dcc has no `double` type. `strtod` is absent entirely.

## `%f` needs the `-ffloatio` build flag

Symptom: `printf("%f", x)` produces nothing useful. Float formatting is only
linked when you compile with `-f` / `-ffloatio`, so any program that prints
floats must be built with that flag.

## 16-bit `int` (and `size_t`)

Symptom: a loop counter, size, index, or accumulator silently wraps past
±32767. `size_t` is 16-bit too, so a single object/allocation can't exceed
~64 KB. Fixes: use `long` + `%ld` for anything that can exceed the range; keep
file offsets in `long` (`ftell`/`fseek`/`lseek`, `off_t`) — never truncate them
into an `int`.

## `char` is signed

Symptom: a byte ≥ 0x80 reads as negative, so `c == 0xE9` is never true and
table indexing / sign-extension misbehaves. Fix: use `unsigned char` for raw
bytes and when indexing a table by character value.

## CP/M filenames: 8.3, uppercase, extension ≤ 3 chars

**The source filename itself must conform to 8.3**, or the build fails.
Symptom: a `.c` whose base name is longer than 8 chars, whose extension is
longer than 3 chars, or that contains extra dots (e.g. `my_long_name.c`,
`parse.test.c`) won't build — ntvcm prints
`argument is not a valid CP/M 8.3 filename`. Fix: rename the source so its base
≤ 8 chars and extension ≤ 3 chars before building. `foo.c` builds `FOO.COM`;
run it as `ntvcm FOO` (uppercase, no ext).

ntvcm's filename mapper also **asserts on any on-disk extension longer than 3
characters**, so delete stray `.DS_Store` files before directory-enumeration
tests or a full `runall.sh` (VS Code / file search may hide them).

## No stack/heap guard

The heap grows up, the stack grows down, and nothing stops them colliding.
Reserve stack with `-stack N` (default 512) for deep recursion or large
automatic arrays, and prefer `static`/global for big buffers. (`spsmash.c`
shows an optional manual stack-depth check.)
