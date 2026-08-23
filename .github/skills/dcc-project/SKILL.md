---
name: dcc-project
description: 'Develop, build, test, debug, and optimize the dcc host toolchain: dcc, dccpeep, dccrtlstrip, dccmake, m80c, l80c, dcc-debug-host, the debugger I/O adapter ABI/example, and DCCRTL.MAC. Use for compiler, MIR backend, optimizer, runtime, GDB/MI debugger, debug metadata/fixtures, regression-suite, or toolchain build work. Use dcc-cpm-z80 for ordinary target applications.'
argument-hint: 'Describe the dcc toolchain task'
---

# dcc project

dcc is a cross toolchain. Host programs emit Z80 assembly and CP/M 2.2
executables:

`dcc -> dccpeep (optional) -> m80c -> dccrtlstrip -> m80c -> l80c -> ntvcm`

Production function bodies are generated from MIR only. AST processing after
parsing is metadata-only; legacy body emission, capture/replay, and speculative
legacy register-allocation retries do not exist.

## Source map

| Surface | Location |
| --- | --- |
| Driver, parser, symbols | `src/dcc/dcc.c`, `dcc_func.c`, `dcc_symbols.c` |
| Preprocessor | `src/dcc/dcc_preproc.c`, `dcc_pp_expr.c` |
| Function-local AST | `dcc_ast.c`, `dcc_ast.h`, `dcc_ast_build.c`, `dcc_ast_gen*.c` |
| Non-emitting AST metadata | `dcc_ast_metadata.c`, `dcc_ast_stmt_meta.c` |
| MIR lowering and verification | `dcc_mir.c`, `dcc_mir.h` |
| MIR selection and cost policy | `dcc_mir_select.c` |
| General homed/spilled emitters | `dcc_mir_homed_cfg.c`, `dcc_mir_spilled_cfg.c` |
| Shared MIR emission | `dcc_mir_emit_common.c`, `dcc_mir_target.c`, `dcc_mir_schedule.c` |
| Exact machine schedules | `dcc_mir_machine_*.c` |
| Peephole optimizer | `src/dccpeep/` |
| Runtime stripper | `src/dccrtlstrip/` |
| Z80 runtime | `DCCRTL.MAC` |
| Full CP/M GDB/MI debugger | `src/dcc_debug_host/` |
| Debugger I/O adapter ABI and example | `src/dcc_debug_host/include/`, `src/dcc_debug_host/examples/io_adapter/` |
| Tests and checked performance | `tests/`, `tests/perf_baselines.csv` |

Add exact schedules to the appropriate `dcc_mir_machine_<family>.c` module,
not the core emitter. Family modules use automatic plan state, export one
dispatcher, and define no writable or read-only global data:

```sh
python3 scripts/audit-c-module-exports.py \
  src/dcc/dcc_mir_machine_attention.c \
  --allow-function mir_try_emit_attention_kernels
```

## C module documentation

Every maintained C and header module under `src/` uses a concise Doxygen-style
file header for human and agent navigation. Tests, archived snapshots,
generated/build output, and external fixtures are outside this blanket rule
unless their own subsystem requires it.

- `@file` and `@brief` identify the module and primary purpose.
- `@par Role` states what the file owns.
- `@par Key entry points` names the principal callable surface when useful.
- `@par Boundary` states what neighboring modules own and what this file does
  not do.

Keep the source map above, the AST map in `dcc_ast.h`, the MIR map in
`dcc_mir_internal.h`, and subsystem maps in private headers current when
adding, removing, renaming, or moving modules. Update a file's header whenever
its ownership or boundary changes. Do not describe `dcc_ast_gen*` as a
production function-body fallback: AST processing after parsing is
metadata/classification support and production body Z80 comes only from
selected MIR candidates.

## Build

Canonical all-tool build:

```pwsh
pwsh ./scripts/build-dcc.ps1
```

That build must produce `dcc-debug-host` and the example I/O adapter shared
library as required outputs in the repository root on macOS, Linux, and
Windows. The example filename is `.dylib`, `.so`, or `.dll` according to
platform.

CMake is the independent compiler build check:

```sh
cmake -S src/dcc -B build/cmake-dcc -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake-dcc --parallel
```

New compiler modules must also be added to `src/dcc/CMakeLists.txt`.
Source `.c` names are lowercase; generated CP/M artifacts are uppercase.

## Focused app loop

Use `dccmake` for one application:

```sh
./dccmake tests/tlong.c dcc-output=TLONG dcc-peep=true
./dccmake tests/tlong.c dcc-output=TLONG dcc-peep=false
```

Honor `tests/_test_overrides.json` when running a binary directly. It owns
arguments, stdin, fixtures, stack size, ignored apps, and nondeterministic
performance exclusions. Always use a timeout:

```sh
timeout 30 ntvcm -p -s:0 build/TLONG.COM
```

On macOS use a Perl alarm wrapper when GNU `timeout` is unavailable.

## Regression runner

```pwsh
pwsh ./scripts/runall.ps1 -Apps tlong -Mode full -RunTimeout 30
pwsh ./scripts/runall.ps1 -Mode fast -FailFast
pwsh ./scripts/runall.ps1 -Mode full -Extended
pwsh ./scripts/runall.ps1 -Mode full -Extended -NoStackCheck
```

`-Mode full` runs peep and nopeep. Parallel execution is isolated under
per-run build directories and uses all cores by default; use `-Serial` only to
diagnose ordering or resource issues.

For MIR work, make focused and release runs strict:

```sh
DCC_MIR_REQUIRE_COMPLETE=1 DCC_MIR_REQUIRE_EMIT=1 \
  pwsh ./scripts/runall.ps1 -Apps app1,app2 -Mode full
```

Do not push compiler/runtime changes until both strict full+extended commands
pass:

```sh
DCC_MIR_REQUIRE_COMPLETE=1 DCC_MIR_REQUIRE_EMIT=1 \
  pwsh ./scripts/runall.ps1 -Mode full -Extended -RunTimeout 30 -FailuresOnly
DCC_MIR_REQUIRE_COMPLETE=1 DCC_MIR_REQUIRE_EMIT=1 \
  pwsh ./scripts/runall.ps1 -Mode full -Extended -NoStackCheck \
  -RunTimeout 30 -FailuresOnly
```

## Tests and baselines

- Expected stdout is `tests/baselines/<app>.txt`.
- Per-app execution details belong in `tests/_test_overrides.json`.
- New runnable apps require a `tests/perf_baselines.csv` row.
- Measure checked baselines with the normal stack-check `runall.ps1` path.
- Update only rows whose workload intentionally changed; never move a baseline
  to hide a compiler or optimizer regression.
- `-Report` is a separate no-stack historical report, not a checked baseline.
- Keep peep and nopeep both non-regressing.

## Debugger host and fixture lifecycle

`dcc-debug-host` is the sole source-debugging backend. ntvcm remains the normal
regression/profiling emulator; do not add GDB/MI debugging behavior back to it.

Every change to `dcc` or `dccpeep` must review its effect on debug symbols and
source debugging, even when debugging is not the change's primary purpose.
Check whether the edit affects source/statement boundaries, scopes, types,
symbols, frame/register/constant locations, instruction addresses, or assembly
line rewriting. Preserve full `-g` metadata and release-identical `-gline`
metadata through both peep and nopeep pipelines, and run the focused debug
metadata tests plus the debugger-host tests whenever one of those contracts may
be affected.

For host or debug-metadata changes:

```sh
cmake -S src/dcc_debug_host -B build/dcc_debug_host_tests \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dcc_debug_host_tests --parallel
ctest --test-dir build/dcc_debug_host_tests --output-on-failure
```

The host consumes a matching adjacent `.COM`/`.DBG` pair produced by
`dccmake -g`. Its B: drive is synthesized in memory from the selected program
and fixtures. Fixture inputs are:

- non-hidden regular files under `fixtures/` beside the selected `.COM`;
- binary `--fixture FILE` arguments; and
- CP/M-text `--text-fixture FILE` arguments, which convert LF to CRLF and append
  Ctrl-Z.

Names must be unique CP/M 8.3 basenames; automatic staging ignores
subdirectories. Ordinary session writes are disposable. `--save-fixtures DIR`
publishes final B: only after normal target exit, excludes the launched `.COM`,
and atomically replaces the destination. Abort, halt, quit, or staging failure
must leave an existing destination untouched. Extracted files retain CP/M
128-byte record padding.

Keep this separate from `tests/_test_overrides.json`, which owns normal
regression-emulator arguments, stdin, and fixture staging. The checked-in VS
Code task copies source-adjacent `*.WTS`, `*.IN`, and `*.DAT` files into
`build/fixtures/` because its selected program is `build/DCCDEBUG.COM`.

For ABI work, keep `src/dcc_debug_host/include/dcc_debug_io_adapter.h`, the
generic host loader, and `examples/io_adapter` synchronized. Machine-specific
port maps and terminal control-key policy belong in adapters. The generic host
must pass terminal bytes through when terminal callbacks are absent.

## Performance investigation

Start from measured machine behavior:

```sh
timeout 30 ntvcm -p -s:0 -g:build/app.prof build/APP.COM
sort -t, -k2,2nr build/app.prof | head
```

The profile `count` column is accumulated Z80 cycles, not invocation count.
Map PCs through linked `.SYM` and module `.PRN` files. For a `CALL`, divide its
profile count by 17 to obtain executions.

Use:

```pwsh
pwsh ./scripts/dccprof.ps1 app
pwsh ./scripts/run-dccpeep-tests.ps1
```

Important rules:

- Fewer source lines, assembly text bytes, or static instructions do not prove
  a runtime win.
- A change can improve nopeep while hiding a profitable dccpeep shape; measure
  both outputs.
- Runtime helpers can dominate whole applications. Profile before changing
  compiler schedules.
- CP/M `.COM` sizes are 128-byte quantized; use `.PRN`/symbol addresses when
  exact linked byte movement matters.
- Host-dispatch-based emulators may price instructions differently from Z80
  T-states. Compare dynamic instruction mix as well as authentic cycles when
  real hardware diverges from ntvcm.

## Runtime ABI rules

- `int`, pointers, and `size_t` are 16-bit; `long` and `float` are 32-bit.
- BC/DE are caller-saved. IY is callee-saved and may hold call-crossing MIR
  values.
- Generated IY users save and restore it. `__extln` preserves it;
  `_setjmp`/`_longjmp` save and restore it in `jmp_buf`.
- Run after every runtime edit:

```sh
python3 scripts/rtl-iy-safety.py
python3 scripts/audit-runtime-coverage.py
```

Runtime blocks are stripped by reference. A new helper that jumps into another
runtime block must retain that dependency explicitly. Validate both its fast
path and fallback through a linked CP/M test.

## Sanitizers and script tests

Use ASan/UBSan when changing CFG, liveness, allocation, ownership, or recursive
proofs:

```sh
cmake -S src/dcc -B build/cmake-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build/cmake-sanitize --parallel
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ./dcc -stack 512 -I . tests/app.c -o build/SANAPP.MAC
pwsh ./scripts/build-dcc.ps1
```

Run repository Python tool tests with:

```sh
python3 -m unittest discover -s scripts/tests -p 'test_*.py'
```

## Change discipline

- Preserve unrelated dirty files and user worktrees.
- Use structural/type/CFG/value proofs, never application or function names.
- Keep semantic gates separate from profitability gates.
- Decline unknown, cyclic, volatile, aliased, or unsupported shapes.
- Use bounded recursion and conservative allocation failure behavior in
  compiler proofs.
- Do not reintroduce legacy generated output as an oracle or fallback.
- For behavior-preserving refactors, compare raw compiler output and
  diagnostics before/after, then run the suite. `tstdc` may differ only in
  embedded `__TIME__`.
- Keep generated census/profile artifacts under `build/`; do not commit them.
- Record durable negative experiments in `mir-text-size-plan.md` and concise
  current state in `plan.md`.

For MIR selector, schedule, allocation, or cost-policy work, also use the
`mir-migration` skill.
