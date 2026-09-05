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
  threads, or POSIX process APIs are provided. The C89 locale, signal, and time
  APIs exist, but return documented `C`-locale, no-op, or unavailable results
  where CP/M 2.2 has no matching service.
- **`scanf` is integer/string only.** Floating input, scansets, `%n`, and `%p`
  are not implemented.
- **No `+`/space/`#` printf flags and no `*` width/precision.** Use literal
  field widths.
- **Formatted-I/O support is selected per call.** Literal `printf`-family
  formats automatically select float, long, hexadecimal, and octal runtime
  paths; non-literal formats conservatively include them. The `-f*io` and
  `-fno-*io` options are force overrides, not normal opt-ins.
- **Wide-character Unicode library behavior is not implemented.** `wchar_t` is a
  16-bit integer typedef, but the DCC C Compiler does not provide a hosted wide-character
  Unicode runtime.
- **`execv()` cannot encode empty or whitespace-containing arguments.** The
  direct CP/M startup parser has no quoting or escaping syntax; quote and
  backslash remain literal for compatibility. `execv()` rejects arguments that
  would split or disappear with `EINVAL`, and both `exec()` and `execv()` reject
  generated command tails above 127 text bytes with `E2BIG`.
- **Executable paths use strict CP/M 8.3 syntax.** `exec()`/`execv()` accept an
  optional `A:` through `P:` drive, a one-to-eight byte name, and an optional
  one-to-three byte filetype; `.COM` is supplied when absent. Invalid paths use
  `EINVAL`, missing files use `ENOENT`, and record-rounded images that cannot
  fit safely in the TPA use `EFBIG`.

## Runtime and environment limits

- **No stack/heap guard by default.** The heap and stack share memory; size the
  stack with `-stack`. There is no protection at runtime *unless* you opt in to
  the lightweight stack-overflow guard with **`-fstack-check`**, which makes an
  overflow exit cleanly with a `?stack overflow` message instead of silently
  corrupting the heap. See [Building and linking](02-build-and-link.md) for the
  flag and the `stacksize` utility that measures the reserve an app needs.
- **CP/M 2.2 is the baseline, not the only usable system.** Ordinary console
  and file I/O use BDOS. Optional services include BIOS wrappers, BDOS 105
  for `time()`, BDOS 108 for exit status, and BDOS 48 for disk flushing.
  Availability of these later-system services depends on the OS or emulator;
  in particular, `fsync()`/`fdatasync()` can terminate a guest on systems that
  reject unsupported calls. See [System and CP/M services](10-system-and-cpm.md).
- **CP/M text files are not byte-stream hosted files.** Text input follows CP/M
  Ctrl-Z EOF conventions, and stdio is intentionally smaller than hosted C
  stdio. File length is tracked in 128-byte records rather than exact bytes,
  wildcard `unlink()` can delete more than one file (`rename()` rejects
  wildcards), and there is no
  subdirectory/path concept beyond a single drive-letter prefix — see
  [File I/O and CP/M BDOS conventions](10-system-and-cpm.md#file-io-and-cpm-bdos-conventions)
  for the full list of C89/POSIX deviations this causes and why they aren't
  "fixed" at the runtime level.
- **Public symbols are significant to only 6 characters.** M80/L80 keep the
  first 6 characters of an external symbol, and DCC C Compiler's leading `_` uses one, so
  every non-`static` function and global must be unique within its first 5
  characters across all linked modules. Make single-file symbols `static` and
  avoid long shared prefixes. See
  [Multi-module symbol names](02-build-and-link.md#multi-module-symbol-names)
  for the collision rule, error messages, and a detection recipe.
