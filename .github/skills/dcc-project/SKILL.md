---
name: dcc-project
description: 'Develop, build, and test the dcc toolchain itself — the host programs dcc (C89-base front end with selected C99/C11 features -> Z80/M80 assembler), dccpeep (peephole optimizer), and dccrtlstrip (runtime stripper), plus the DCCRTL.MAC Z80 runtime. Use when modifying or debugging compiler/optimizer/runtime sources under src/, running the regression suite (runall.ps1), building one app (dccmake), or rebuilding the host tools (build-dcc.ps1). NOT for writing ordinary C apps that target CP/M — use the dcc-cpm-z80 skill for that.'
argument-hint: 'Describe the dcc-project task (change codegen, run the test suite, build a single app, rebuild host tools)'
---

# dcc project (compiler / optimizer / runtime development)

dcc is a **cross** toolchain: the host programs `dcc`, `dccpeep`, and
`dccrtlstrip` compile with a modern compiler and run on your desktop. They emit
Z80 assembly and CP/M 2.2 `.COM` files that run under an emulator such as
**ntvcm**. This skill is about changing and validating *those tools and the
runtime*, not about authoring CP/M apps (use `dcc-cpm-z80` for that).

## When to use

- Editing compiler/optimizer/runtime sources under `src/` or `DCCRTL.MAC`.
- Running the regression suite or reproducing a single test failure.
- Rebuilding the host tools after a source change.

## Toolchain pipeline

One `.c` file becomes a `.COM` through a short pipeline (each stage hands a file
to the next):

`dcc` (.c → .MAC) → `dccpeep` (.MAC → .MAC, optional) + `dccrtlstrip`
(DCCRTL.MAC → RTLMIN.MAC, keep only referenced routines) → `m80c` (assemble) →
`L80` (link → .COM). `dccmake` uses native host `m80c` by default and runs
Microsoft's `L80` under ntvcm; `dcc-use-emulated-m80=true` selects Microsoft's
`M80` under ntvcm for assembly instead.

## Source layout

| Path | What |
| ---- | ---- |
| `src/dcc/` | The compiler. `dcc.c` is the driver; `dcc_preproc.c` owns macros/lexer and `dcc_pp_expr.c` owns `#if` expressions. `dcc_func.c` parses functions/top-level declarations, `dcc_global_init.c` records file-scope initializers, and `dcc_regalloc.c`/`dcc_loop_regalloc.c` own speculative register allocation. `dcc_array_narrow.c` proves byte narrowing. **Codegen is a single AST path**: `dcc_ast.c`/`dcc_ast_build.c` build typed function-local ASTs and `dcc_ast_gen*.c` emits them. Low-level helpers live in `dcc_expr.c`, `dcc_ops.c`, `dcc_cmp.c`, `dcc_assign.c`, `dcc_stmt.c`, and `dcc_decl.c`. Focused contracts use `dcc_ast_gen_internal.h`, `dcc_preproc_internal.h`, and `dcc_regalloc_internal.h`; shared state is defined in `dcc_state.c`. |
| `src/dccpeep/` | Fixpoint peephole optimizer (`-Ot` time / `-Os` size). `dccpeep.c` owns the order-sensitive scheduler and remaining general passes; `peep_lines.c` owns the mutable line program and opaque user-asm barriers; `peep_parse.c` and `peep_analyze.c` provide shared parsing/safety analysis. Pass families live in `peep_pass_once.c` (micro-pattern dispatcher), `peep_pass_minmax.c` (board/game idioms), `peep_pass_loops.c` (loop registerization), and `peep_pass_inline_temp.c` (compiler-tagged spills); `peep_pass_stubs.c` and `peep_pass_final.c` own post-convergence size and cleanup passes. |
| `src/dccrtlstrip/` | Runtime dead-block stripper. |
| `DCCRTL.MAC` | The Z80-assembly C runtime (entrypoint, heap, argv, libc subset, float). |
| `tests/` | `*.c` test apps + `tests/baselines/<app>.txt` expected stdout + `tests/_test_overrides.json` (per-app args/stdin/stack/ignore). |
| `scripts/` | `runall.ps1`, `runall-extended.ps1`, `build-dcc.ps1`, `stacksize.*`. |
| `docs/docs/en/appendix/00-architecture.md` | In-depth architecture reference. |

Convention: source `.c` files are **lowercase** (only dcc reads them); generated
`.MAC`, `.REL`, `.PRN`, `.COM` are **UPPERCASE** (CP/M filenames). Matters on
case-sensitive (Linux) filesystems.

## Prerequisites

The scripts expect the `ntvcm` emulator on your `PATH` (it runs `L80`, optional
emulated `M80`, and the built `.COM` files), along with the host tools `dcc`,
`dccpeep`, `dccrtlstrip`, and `m80c` — these land in the repo root after a build,
so add the repo root and ntvcm's directory to `PATH`. Override tools with the
corresponding environment/settings controls when they are not on `PATH`.

## Run the regression tests

Builds every `tests/*.c` app and diffs stdout against its baseline. Runs in
parallel by default; the stack-overflow guard (`-fstack-check`) is on by default.

```pwsh
pwsh ./scripts/runall.ps1                 # default: fast, optimized CP/M Z80 binary
pwsh ./scripts/runall.ps1 -Help           # show help and exit
pwsh ./scripts/runall.ps1 -Mode fast      # default unless otherwise stated by the agent/developer
pwsh ./scripts/runall.ps1 -Mode nopeep    # unoptimized CP/M Z80 binary
pwsh ./scripts/runall.ps1 -Serial         # sequential fallback (debugging)
pwsh ./scripts/runall.ps1 -Extended       # also run the c-testsuite extended corpus
pwsh ./scripts/runall.ps1 -KeepBuild      # keep build/run-<pid>/ for debugging
```

In parallel mode each invocation is isolated under a per-invocation
`build/run-<pid>/` folder that is removed automatically on exit (so `build/`
does not fill up with one folder per run); pass `-KeepBuild` to retain it for
inspection. `runall-extended.ps1` uses the same `run-<pid>` isolation, cleanup,
and `-KeepBuild` behavior under `build/extended-tests/`.

The default `-Mode fast` builds each app once as an optimized CP/M Z80 binary.
Use `-Mode full` to build each app twice, once optimized and once unoptimized,
and verify both against the same baseline. Exit code 0 = all passed, 1 = one or
more failed. Add `-Report` to append per-app cycle/size metrics to
`perf_results.csv`. Add `-Extended` when a regular regression run should also
run the imported c-testsuite single-exec corpus through `runall-extended.ps1`;
that runner initializes the `tests/extended-tests` submodule automatically when
the corpus is not present on disk.

## Test baselines and overrides

Each runnable `tests/<app>.c` test has expected stdout in
`tests/baselines/<app>.txt`. `runall.ps1` builds the app, runs the resulting
`.COM` under the emulator, normalizes line endings for comparison, and checks
that stdout matches the baseline for that app. In `-Mode full`, the fast and
nopeep builds must both match the same baseline; a baseline mismatch means the
program output changed and should be investigated before updating the expected
text.

`tests/_test_overrides.json` is the per-app run configuration used by
`runall.ps1`. Use it instead of hard-coding special cases in the runner:

- `args`: command-line arguments passed to the CP/M app (for example interpreter
	input files or test depth flags).
- `stdin`: text piped to the app's stdin for keyboard/input-oriented tests.
- `stack_size`: per-app stack reserve override when the default stack is too
	small.
- `ignore`: skip an app that should not be built or compared in the full suite.

When adding or changing a test, update `_test_overrides.json` for its runtime
needs first, then regenerate or edit `tests/baselines/<app>.txt` only when the
new output is the intended behavior. New runnable tests need a
`tests/perf_baselines.csv` row; expanding an existing test's workload normally
requires updating that row too. Measure both modes with `-Mode full` and change
only the affected row/columns.
Measure those values with the normal `runall.ps1` stack-check build (for
example `pwsh ./scripts/runall.ps1 -Mode full`, then copy the reported new
app/mode cycle counts) rather than ad-hoc `dccmake` runs, because stack-check
changes the cycle counts. Avoid broad `-UpdatePerfBaseline` updates unless the
task explicitly requires them. `-Report` is a separate no-stack-check historical
report and must not supply checked performance baselines.

When running test apps directly under `ntvcm` for benchmarking or debugging,
look up the app in `_test_overrides.json` first and pass the same `args`,
`stdin`, and stack assumptions that `runall.ps1` would use. Some apps are
interpreters, expect keyboard input, or are intentionally ignored; raw direct
`ntvcm APP.COM` runs can hang or measure the wrong workload. On macOS, if no
`timeout` command is installed, use a small Perl alarm wrapper for ad-hoc direct
runs, for example:

```sh
perl -e 'alarm shift; exec @ARGV' 30 ntvcm -p -s:0 APP.COM ARGS...
```

## Build / debug a single app

Use `dccmake` to drive the full pipeline for one app — ideal for reproducing a
failing test in isolation:

```sh
dccmake tests/sieve.c dcc-output=SIEVE dcc-peep=true
dccmake tests/sieve.c dcc-output=SIEVE dcc-peep=false
```

`dccmake` accepts positional `.c` inputs or `dcc-input=main.c,module.c`, and the
output base must be CP/M 8.3-clean. Common settings are:

```sh
dccmake tests/app.c dcc-output=APP dcc-peep=true dcc-stack-bytes=768
dccmake main.c module.c dcc-output=APP dcc-include-directory=include
dccmake tests/e.c dcc-output=E
```

Literal `printf`-family formats select float and long runtime variants per call
without flags. Use `dcc-floatio=true` / `dcc-flongio=true` only when a test must
force those variants globally; the suite's explicit overrides are also used to
exercise each formatted-I/O runtime entry point deliberately.

To compare a suspected optimizer bug, build once with `dcc-peep=true` and once
with `dcc-peep=false`, then diff the run output or generated `build/<NAME>.MAC`.
Tool commands can be pinned with settings such as `dcc-tool=./dcc`,
`dccpeep-tool=./dccpeep`, `dccrtlstrip-tool=./dccrtlstrip`, and
`ntvcm-tool=ntvcm`; `DCC_AST_REPORT=1` logs `; AST-unsupported ...` for the
statement/initializer a support gate declined, and `DCC_AST_BUILD=2` dumps each
built AST tree to stderr before it is emitted.

## Rebuild the host tools after a source change

For compiler-only edits, the fastest host build is:

```sh
sh src/dcc/build-dcc.sh
```

It links every `src/dcc/*.c`; when adding a module, also add it to the explicit
`src/dcc/CMakeLists.txt` source list.

The `dcc` implementation is host code, not code for the Z80 target. It may use
portable C11 supported by modern Clang, GCC, and MSVC; do not constrain it to
the language subset that dcc accepts as input. Keep vendor-only extensions
behind platform guards.

```pwsh
pwsh ./scripts/build-dcc.ps1            # MSVC on Windows, clang on macOS, gcc on Linux
```

Or the platform root scripts: `m.bat` (Windows/MSVC), `m.sh` (Linux/gcc),
`mmacos.sh` (macOS/clang). All three produce `dcc`, `dccpeep`, `dccrtlstrip` in
the repo root. Rebuild before re-running `runall.ps1` so tests exercise your
change.

## Performance and optimizer work

Use measured signals before changing codegen, `dccpeep`, or `DCCRTL.MAC`:

- Run `pwsh ./scripts/run-dccpeep-tests.ps1` for direct optimizer fixtures.
	Fixture stems ending in `.os` run with `-Os`; stems ending in `.undoc` run
	with `-fundocumented-z80`. Use `dccpeep -fstats input.mac output.mac` for
	iteration, pass-change, and line-mutation counts without changing output.
- Pure `dccpeep` refactors must produce byte-identical optimized `.MAC` output
	over the saved raw compiler-output corpus. Optimizer improvements may lower
	checked peep cycle/size baselines, but must never raise them or change nopeep
	columns. Shared `-Os` helpers must meet their complete linked-stub break-even
	count before rewriting.

- For cycle measurements, run CP/M binaries with `ntvcm -p -s:0` and compare
	the reported `Z80 cycles`; full-speed execution does not change the emulated
	cycle total.
- For direct benchmark runs, honor `tests/_test_overrides.json` and use a
	timeout/alarm wrapper so input-driven or long-running apps do not hang the
	session.
- If the local `ntvcm` build has the profiling extension, `-g:<file>` writes a
	`pc,count,asm` CSV. Sort it with `sort -t, -k2 -nr file.prof | head` and map
	hot PCs back to generated `.PRN`/`.MAC` or runtime labels before optimizing.
- For broad compiler-vs-peephole comparisons, keep the peephole version fixed
	and compare post-peephole instruction counts. Peephole tag counts alone can
	mislead: fewer tags may mean the compiler emitted the optimized form directly.

Important performance lessons from recent work:

- `dccpeep` has many shape-specific passes. A compiler change that improves
	no-peep output can still regress the shipping path if it hides canonical loop
	shapes such as stride loops or compare-fusion patterns. Check peep output and
	dynamic cycles before keeping such changes.
- Prefer small, falsifiable peephole passes with tight guards. Good generic
	candidates are repeated residual patterns across many optimized `.MAC` files,
	especially when the next consumer proves registers/flags are dead. Exclude
	register-ABI helpers such as `__call_hl` and `__m1s` from ordinary
	stack-argument rewrites.
- Runtime helper changes can dominate app performance. Profile first: fixed
	point and long-heavy apps often spend most cycles in `DCCRTL.MAC` helpers such
	as multiply, divide, shift, or string/memory routines.
- `DCCRTL.MAC` is copied by `dccrtlstrip`; do not rely on assembler macro
	features such as `REPT` unless the runtime/tooling already supports them.
	Manual unrolling should be size-bounded and justified by measured wins.
- For AST constant folding, avoid host undefined behavior and host-only
	semantics. Fold only when target signed/unsigned behavior is provably the
	same, and use unsigned host arithmetic for low-bit shift folds when needed.
- Proof-based optimizations must conservatively decline unknown or recursive
	shapes. Recursive walks over captured ASTs need cycle/depth guards.
- `EmitSink` purpose (FINAL/DISCARD/VERIFY/DEFERRED) describes the destination,
	not suppression. Do not blanket-convert raw formatted writes to a
	`scan_mode`-guarded emitter: verification buffers may need those bytes.

## Behavior-preserving compiler refactors

For parser/codegen restructuring, build before/after compilers and require zero
`.MAC` differences across `tests/*.c` plus zero stderr differences across
`tests/diagnostics/*.c`, then run `runall.ps1`. Move a new untracked `.c` module
aside while building the baseline because `build-dcc.sh` globs all sources.

Useful corpus-mining tactics:

- Build a deterministic sample from `tests/*.c` by sorted filename when a full
	corpus pass is too slow; record the sample rule and failures/ignored apps.
- Mine optimized `.MAC` output for repeated n-grams after stripping comments and
	labels, then inspect concrete contexts before writing a pass.
- Validate a new pass with: rebuild host tools, rebuild affected apps, count the
	new `; peep:` tag, compare size/cycles on affected apps, then run
	`pwsh ./scripts/runall.ps1 -Mode full`.

## Typical workflow

1. Change a source file under `src/` (or `DCCRTL.MAC`).
2. `pwsh ./scripts/build-dcc.ps1` to rebuild the host tools.
3. `dccmake tests/<app>.c dcc-output=<APP> dcc-peep=true` to reproduce/iterate
	on one case.
4. `pwsh ./scripts/runall.ps1` to confirm no regressions across all apps; use
   `pwsh ./scripts/runall.ps1 -Extended` when the extended c-testsuite corpus
   should be included too.
