# Appendix: runtime optimization

`DCCRTL.MAC` is a single ~19,000-line runtime, but most programs use only a
fraction of it. The normal DCC C Compiler build flow runs `dccrtlstrip` before the final
L80 link to remove unreferenced routines. This appendix explains how it decides
what to keep and what each library feature costs in code size once its
transitive dependencies are linked.

## How `dccrtlstrip` decides what to keep

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
  in the always-present baseline. This is the true incremental cost of using
  that function in a program that otherwise wouldn't need it.
- **pulls in** = the extra runtime blocks added beyond the baseline.

The rest of this page explains the *structure* the numbers reflect — the
always-present baseline and the shared cores that make the first call into a
feature expensive — and the optimisation takeaways that follow from it.

## The always-present baseline

Every program links these regardless of what it calls, because `start` is a
forced root:

| Block | Role |
| --- | --- |
| `start` | entry, heap init, BSS zeroing, calls `_main` |
| `__build_argv` (+ `__conout`, `__argbuf`, `argv`) | command-tail argv builder; **also holds `__conout`**, the console writer |
| `__brk`, `__hlimit` | heap state words |
| `_exit` (+ `__cpm_set_retcode`) | reached from `start` after `_main` returns |

Because `__conout` lives inside the `__build_argv` block, **console output costs
nothing extra** — `putchar`/`puts` call already-present code. (See
[*Runtime function sizes*](02-runtime-sizes.md) for the exact baseline line
count.)

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

- **Formatted I/O.** Integer `printf` is a self-contained monolith; a
  `printf`-family call whose format needs `%f` links the entire float stack on
  top of it. Literal formats are analyzed automatically; non-literal formats
  conservatively select all optional paths.
  `sprintf`/`vprintf`/`vsprintf` reuse the formatter for free, while
  `fprintf`/`vfprintf` carry the file-I/O core.
- **`scanf` family.** `scanf`/`sscanf` are tiny stubs that jump into the shared
  `fscanf` core, so using any one links all three plus the read path.
- **Low-level file I/O.** `open`/`read`/`write`/`close`/`lseek`/`unlink`/
  `fsync`/`fdatasync` share one FCB/DMA core. Using any one links that core.
- **Memory.** `malloc`/`calloc`/`realloc`/`free` link the heap helpers
  (`__mlh`, `__frcoal`); `calloc` adds overflow-checked size arithmetic.
- **32-bit `long`.** Multiply/divide/modulo route through a small set of long
  helpers (`__lmd`, `__lmu`, …); the compare operators are self-contained.
- **Float.** A single `float` operator links the shared normalise/round core.

!!! tip "`frexpf` / `ldexpf` are the cheap float functions"
    They manipulate IEEE-754 bits directly (no arithmetic core), so they only
    link a couple of classify helpers. Everything else requires the full float
    arithmetic stack, and the `exp`/`log`/`pow` and hyperbolic group is the most
    expensive group to link: budget ~2,000–3,300 lines for any one of them.

String and ctype routines are the exception: almost all are self-contained and
link nothing beyond themselves. `strdup` is the notable outlier: it allocates,
so it inherits the whole `malloc` chain.

## Optimisation takeaways

1. **Console-only output is cheap.** `putchar`, `puts`, and integer `printf`
   only touch already-present code or are self-contained. Avoid
   `fputc`/`fputs`/`fprintf` for console work — they link the file-I/O core.
2. **`printf` is an 842-line monolith** but links nothing else. **A `%f`
  formatted-output call roughly triples that** by linking the entire float stack.
   `vprintf`/`vsprintf` reuse that engine for free; `vfprintf` carries the
   file-I/O core like `fprintf`.
3. **Any single low-level file call links the whole FCB/DMA core (~470 lines).**
   The first file function is expensive; additional ones are nearly free.
4. **`scanf`/`sscanf` are not small** — they share the 697-line `fscanf` core.
5. **Float is the biggest lever.** A single `float` operator links ~700+ lines;
   `sqrtf`/`fmodf` exceed 1,300 lines, and the `expf`/`logf`/`powf` and
   hyperbolic group runs ~2,000–3,300 lines.
6. **`malloc`/`calloc` link integer mul/div/mod helpers** for size arithmetic;
   `strdup` inherits the whole `malloc` chain.
7. **String/ctype routines are individually cheap** — they link only themselves.

The practical rule: every call either stays cheap or links a substantial amount
of support code. Use the console functions, integer-only `printf`, and the
self-contained string helpers when binary size matters. Treat float formatting
and transcendental math functions as deliberate, budgeted choices.
