# Limitations

Keep these constraints in mind; they follow directly from the 16-bit CP/M target
and the single source-of-truth runtime.

## Language and type limits

- **Language support is defined on the conformance page.** See
  [C conformance and target exceptions](01-c-conformance.md) for the additive
  C89/C99/C11 feature matrix, target model, and practical numeric implications.
- **Treat hosted-desktop assumptions as out of scope for this target.** Code
  written for ILP32/LP64/LLP64 host ABIs, full hosted C libraries, or 64-bit
  arithmetic should be treated as a porting task.

## Library limits

- **DCCRTL is a CP/M runtime subset, not hosted libc.** No pthreads, C11
  threads, POSIX process APIs, signals, locale, or time library are provided.
- **`scanf` is integer/string only.** Floating input, scansets, `%n`, and `%p`
  are not implemented.
- **No `+`/space/`#` printf flags and no `*` width/precision.** Use literal
  field widths.
- **`%f` needs `-ffloatio`.** Without that flag, float formatting isn't linked.
- **Wide-character Unicode library behavior is not implemented.** `wchar_t` is a
  16-bit integer typedef, but the DCC C Compiler does not provide a hosted wide-character
  Unicode runtime.

## Runtime and environment limits

- **No stack/heap guard by default.** The heap and stack share memory; size the
  stack with `-stack`. There is no protection at runtime *unless* you opt in to
  the lightweight stack-overflow guard with **`-fstack-check`**, which makes an
  overflow exit cleanly with a `?stack overflow` message instead of silently
  corrupting the heap. See [Building and linking](02-build-and-link.md) for the
  flag and the `stacksize` utility that measures the reserve an app needs.
- **CP/M 2.2 only.** The runtime uses BDOS functions only (no BIOS calls), plus
  CP/M 3.0 BDOS 108 for the process exit code.
- **CP/M text files are not byte-stream hosted files.** Text input follows CP/M
  Ctrl-Z EOF conventions, and stdio is intentionally smaller than hosted C
  stdio.

## Declared but not in the runtime

Some functions are declared in `stdlib.h` for source compatibility but are
**not** implemented in `DCCRTL.MAC`. If you call them without supplying your own
definition, the link step fails with an unresolved external.

| Function | Notes |
| --- | --- |
| `atof` | Implemented and declared as `float atof(const char *)`. C89 `atof` returns `double`, which the DCC C Compiler does not have; this extension returns `float` (IEEE 754 single precision). |

If you need to supply your own implementation of an unimplemented function,
either `#include` its `.c` from your main file, or compile it separately with
`dcc -c` and link the resulting `.REL` (the separate-compilation workflow).
