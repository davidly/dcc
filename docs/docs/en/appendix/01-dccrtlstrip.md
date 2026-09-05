# Appendix: application and runtime optimization

`DCCRTL.MAC` is a single assembly runtime, but most programs use only a
fraction of it. Application modules can also contain functions and objects that
the final program never reaches. The normal DCC C Compiler build flow runs
`dccrtlstrip` before assembly to remove unreachable application blocks, then
uses the reduced application to select only the required runtime routines.
This appendix explains both passes.

## Whole-program application stripping

`dccmake` enables whole-program stripping by default after `dccpeep` and before
`m80c`. The pass analyzes every input module together, starting at the
`__mrun` program entry, and follows direct calls, jumps, data addresses,
function-pointer table entries, global initializers, internal labels, and
cross-module `PUBLIC`/`EXTRN` references to a fixed point.

Unreachable functions, initialized global/static objects, and uninitialized
objects in helper modules are omitted before assembly. Automatic local
variables remain the compiler's responsibility, and primary-module BSS remains
packed in the application's shared BSS layout.

| Program element | Whole-program behavior |
| --- | --- |
| Uncalled functions | Removed, including functions defined in another source module |
| Initialized globals and file-scope statics | Removed when no reachable code or initializer references them |
| Uninitialized helper-module globals/statics | Removed from that module's ordinary `DS` storage when unreachable |
| Primary-module uninitialized globals/statics | Retained because they share the packed `__bssb`/`__bssn` layout |
| Automatic local variables | Not an LTO object; dead locals and stores are handled earlier by MIR analysis and instruction selection |
| String literals | Retained in the first implementation to avoid address-layout performance changes that do not reduce the 128-byte-quantized `.COM` size |

Use the normal multi-module build command; no source annotation is required:

```sh
dccmake main.c module.c dcc-output=APP
```

For a helper module intended for a separate later link, compile it directly
with `-module` and do not application-strip it before the final roots are known:

```sh
dcc -module -I /path/to/dcc module.c -o MODULE.MAC
m80c "=MODULE.MAC" /X /O /Z /L
```

`dccmake` always attempts a complete application link; `dcc-strip-unused=false`
disables its stripping pass but does not turn it into a compile-only driver.

The direct application-strip mode rewrites the listed dcc-generated `.MAC`
files transactionally:

```sh
dccrtlstrip --strip-apps [-k root]... app.mac [module.mac ...]
```

`-k`/`-root` adds an explicit application root. `__mrun` is always rooted.
Malformed structural markers or duplicate public definitions fail loudly
before a partially analyzed module set is installed.

## Runtime-strip direct usage

The normal build helpers invoke `dccrtlstrip` automatically. To run it directly:

```sh
dccrtlstrip [-k symbol]... -r DCCRTL.MAC -o RTLMIN.MAC app.mac [app2.mac ...]
```

| Option or argument | Purpose |
| --- | --- |
| `-r <file>` | Full runtime source to analyze |
| `-o <file>` | Reduced runtime file to write |
| `-k <symbol>` | Keep an explicit runtime root; repeat for additional symbols |
| `-root <symbol>` | Alias for `-k` |
| `app.mac ...` | One or more application assembly modules to scan for runtime references |

## How runtime stripping decides what to keep

Most library names in the standard headers are ordinary C identifiers. During
code generation, DCC C Compiler maps well-known library calls to short internal assembler
labels (for example `memcpy` becomes `__mcpy`, `strlen` becomes `__slen`). Do
not write those short names yourself; include the header and call the C
function. These internal names are what `dccrtlstrip` sees when it scans the
generated `.MAC` file.

`dccrtlstrip` is a conservative dead-block eliminator that runs **before** L80
linking. Its flow:

1. **Split into blocks.** `DCCRTL.MAC` is split into blocks delimited by
   `public` directives. A run of consecutive `public` lines becomes a shared
   *prelude* block, and each real public label after it becomes its own block
   that *depends* on the prelude. Everything before the first `public` (the
   `org 100h`, the `extrn` declarations, the `errno` EQUs, `HDRSIZE`) is an
   unconditional preamble.
2. **Scan the app for references.** For each app `.mac`, opcodes are parsed and
   their symbol operands recorded as *roots* (`extrn`, `call`, `jp`, `jr`, `dw`,
   and `ld` forms). A fallback whole-token scan also treats any exact mention of
   a known runtime symbol as a root.
3. **Mark reachable blocks.** `start` is forced as a root. Each root's owning
   block (plus its prelude) is kept, then the kept blocks are re-scanned for
  further references, iterating to a fixpoint. **Transitive runtime-to-runtime
  dependencies are therefore linked automatically.**
4. **Write the output.** The preamble is emitted unconditionally, then only the
   kept blocks; `public` lines are filtered so only kept symbols are
   re-declared.

### Design consequences

- **Transitivity is automatic** — keeping `_printf` re-scans its body and links
  the `pf_*` helpers; keeping a float op links the classify helpers.
- **The fallback scan is deliberately over-conservative** — any mention of a
  runtime symbol's exact name keeps it. dcc emits the matching formatted-output
  entry point after per-call format analysis, so its selected float/long paths
  are retained automatically.
- **Unused features cost nothing** — a program that never does `float`
  arithmetic keeps none of the float blocks.

## How to read the size numbers

The per-function size tables live on a dedicated, **auto-generated** page —
[*Runtime function sizes*](02-runtime-sizes.md) — which is rebuilt from
`DCCRTL.MAC` on every docs build so the numbers never drift. Each routine is
reported with three figures:

- **self** = source lines in the function's own block.
- **marginal** = self + every *additional* reachable block that is not already
  in the always-present baseline. This estimates incremental source volume,
  not linked bytes or execution time.
- **pulls in** = the extra runtime blocks added beyond the baseline.

The rest of this page explains the *structure* the numbers reflect — the
always-present baseline and the shared cores that make the first call into a
feature expensive — and the optimisation takeaways that follow from it.

## The always-present baseline

Every program links these regardless of what it calls, because `start` is a
forced root:

| Block | Role |
| --- | --- |
| `start` | entry, heap init, BSS zeroing, calls the application's `__mrun` main shim |
| `__brk`, `__hlimit` | heap state words |
| `_exit` and its dependencies | normal termination, console flushing, and exit status |

The command-tail builder and console writer have separate public blocks.
Application references, including those from `__mrun`, can retain additional
startup support beyond the table hook's `start`-only baseline. Console output
is small, but its wrappers and dependencies are not literally free. See
[*Runtime function sizes*](02-runtime-sizes.md) for current source-line counts.

## The shared cores

The runtime's size is dominated by a handful of shared cores. A feature's
*first* call links the whole core; additional calls in the same family are then
nearly free. This is why the `marginal` column on the
[sizes page](02-runtime-sizes.md) can dwarf a routine's `self` count.

!!! warning "Console-only output: avoid the file-stream functions"
    `fputc`/`fputs`/`fprintf` are **not** lightweight even when you only ever
    target the console — they dispatch on the file descriptor and therefore link
    the whole low-level file-I/O core. For console-only output prefer
    `putchar`/`puts`/`printf`.

- **Formatted I/O.** The `printf` family shares a base engine with optional
  float, long, hexadecimal, and octal paths. Literal formats select those
  paths per call; non-literal formats conservatively select all of them.
  String and `va_list` wrappers reuse the engine but add their own code;
  `fprintf`/`vfprintf` also require file-I/O support.
- **`scanf` family.** `scanf`/`sscanf` are tiny stubs that jump into the shared
  `fscanf` core, so using any one links all three plus the read path.
- **Low-level file I/O.** `open`/`read`/`write`/`close`/`lseek`/`unlink`/
  `fsync`/`fdatasync` share one FCB/DMA core. Using any one links that core.
- **Memory.** `malloc`/`calloc`/`realloc`/`free` link the heap helpers
  (`__mlh`, `__frcoal`); `calloc` adds overflow-checked size arithmetic.
- **32-bit `long`.** Multiply/divide/modulo route through a small set of long
  helpers (`__lmd`, `__lmu`, …); the compare operators are self-contained.
- **Float.** A single `float` operator links the shared normalise/round core.

Bit-oriented float operations can cost much less than transcendental math.
Consult the generated table for their current dependency estimates; do not
assume every float function pulls in the same arithmetic support.

String and ctype routines are the exception: almost all are self-contained and
link nothing beyond themselves. `strdup` is the notable outlier: it allocates,
so it inherits the whole `malloc` chain.

## Optimisation takeaways

1. Prefer console-specific functions for console-only output.
2. Keep format strings literal when practical so per-call format selection can
  omit unused paths.
3. Budget for the shared support introduced by the first file, formatted-input,
  allocation, or floating-point operation.
4. Distinguish allocation from checked size arithmetic: `calloc` adds
  multiplication checks and zero-filling; `malloc` does not inherently need
  the same chain.
5. Measure the actual `.COM` and inspect assembler listings for byte costs.
  Source-line counts include comments and blank lines and are not a byte-size
  or performance guarantee. CP/M file sizes are rounded to 128-byte records.

The practical rule: every call either stays cheap or links a substantial amount
of support code. Use the console functions, integer-only `printf`, and the
self-contained string helpers when binary size matters. Treat float formatting
and transcendental math functions as deliberate, budgeted choices.
