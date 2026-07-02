---
name: dcc-project
description: 'Develop, build, and test the dcc toolchain itself — the host programs dcc (C89/C99/C11 front end -> Z80/M80 assembler), dccpeep (peephole optimizer), and dccrtlstrip (runtime stripper), plus the DCCRTL.MAC Z80 runtime. Use when modifying or debugging compiler/optimizer/runtime sources under src/, running the regression suite (runall.ps1), building one app (ma.ps1), or rebuilding the host tools (build-dcc.ps1). NOT for writing ordinary C apps that target CP/M — use the dcc-cpm-z80 skill for that.'
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

`dcc` (.c → .MAC) → `dccpeep` (.MAC → .MAC, optional) → `M80` (assemble) +
`dccrtlstrip` (DCCRTL.MAC → RTLMIN.MAC, keep only referenced routines) → `M80` →
`L80` (link → .COM). `M80`/`L80` are Microsoft's assembler/linker, run under
ntvcm.

## Source layout

| Path | What |
| ---- | ---- |
| `src/dcc/` | The compiler. `dcc.c` driver; phases split across `dcc_preproc.c`, `dcc_decl.c`, `dcc_expr.c`, `dcc_stmt.c`, `dcc_func.c`, `dcc_ops.c`, `dcc_fold.c`/`dcc_constexpr.c` (folding), `dcc_types.c`, `dcc_symbols.c`, `dcc_data.c`, `dcc_diag_emit.c`. **Codegen is a single AST path**: `dcc_ast.c`/`dcc_ast_build.c` build the typed function-local AST (initializers via `ast_emit_init_expr` into an isolated arena), and the AST emitter lives in `dcc_ast_gen.c` + `dcc_ast_gen_support.c`/`_expr.c`/`_cond.c`/`_stmt.c` (behind `dcc_ast_gen_internal.h`). The `dcc_expr.c`/`dcc_ops.c`/`dcc_cmp.c`/`dcc_assign.c`/`dcc_stmt.c` modules provide the low-level emit helpers the AST walker calls into. |
| `src/dccpeep/` | Peephole optimizer (`-Ot` time / `-Os` size). |
| `src/dccrtlstrip/` | Runtime dead-block stripper. |
| `DCCRTL.MAC` | The Z80-assembly C runtime (entrypoint, heap, argv, libc subset, float). |
| `tests/` | `*.c` test apps + `tests/baselines/<app>.txt` expected stdout + `tests/_test_overrides.json` (per-app args/stdin/stack/ignore). |
| `scripts/` | `runall.ps1`, `runall-extended.ps1`, `ma.ps1`, `build-dcc.ps1`, `stacksize.*`. |
| `docs/docs/en/appendix/00-architecture.md` | In-depth architecture reference. |

Convention: source `.c` files are **lowercase** (only dcc reads them); generated
`.MAC`, `.REL`, `.PRN`, `.COM` are **UPPERCASE** (CP/M filenames). Matters on
case-sensitive (Linux) filesystems.

## Prerequisites

The scripts expect the `ntvcm` emulator on your `PATH` (it runs `M80`/`L80` and
the built `.COM` files), along with the host tools `dcc`, `dccpeep`, and
`dccrtlstrip` — these land in the repo root after a build, so add the repo root
and ntvcm's directory to `PATH`. Override any tool individually with the
`NTVCM`/`DCC`/`DCCPEEP`/`DCCRTLSTRIP`/`M80`/`L80` env vars if it isn't on `PATH`.

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
new output is the intended behavior.

## Build / debug a single app

Use `ma.ps1` to drive the full pipeline for one app — ideal for reproducing a
failing test in isolation:

```pwsh
pwsh ./scripts/ma.ps1 <name>            # full (optimized + unoptimized) — default
pwsh ./scripts/ma.ps1 <name> fast       # optimized CP/M Z80 binary
pwsh ./scripts/ma.ps1 <name> nopeep     # unoptimized CP/M Z80 binary
```

`<name>` is the test/app name without `.c` (e.g. `sieve`, `ttt`, `cobint`). To
compare a suspected optimizer bug, build both ways and diff the run output or
the generated `BUILD/<NAME>.MAC`. Useful env vars: `DCC_STACK_SIZE` (stack
reserve), `DCC_FORCE_STACK_CHECK=1`, and `DCC`/`DCCPEEP`/`DCCRTLSTRIP`/`NTVCM`/
`M80`/`L80` to pin tool paths. For AST codegen debugging: `DCC_AST_REPORT=1`
logs `; AST-unsupported ...` for the statement/initializer a support gate
declined (this immediately precedes the `unsupported AST statement` fatal), and
`DCC_AST_BUILD=2` dumps each built AST tree to stderr before it is emitted.

## Rebuild the host tools after a source change

```pwsh
pwsh ./scripts/build-dcc.ps1            # MSVC on Windows, clang on macOS, gcc on Linux
```

Or the platform root scripts: `m.bat` (Windows/MSVC), `m.sh` (Linux/gcc),
`mmacos.sh` (macOS/clang). All three produce `dcc`, `dccpeep`, `dccrtlstrip` in
the repo root. Rebuild before re-running `runall.ps1` so tests exercise your
change.

## Typical workflow

1. Change a source file under `src/` (or `DCCRTL.MAC`).
2. `pwsh ./scripts/build-dcc.ps1` to rebuild the host tools.
3. `pwsh ./scripts/ma.ps1 <app>` to reproduce/iterate on one case.
4. `pwsh ./scripts/runall.ps1` to confirm no regressions across all apps; use
   `pwsh ./scripts/runall.ps1 -Extended` when the extended c-testsuite corpus
   should be included too.
