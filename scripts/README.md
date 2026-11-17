# dcc helper scripts

Developer utility scripts for the `dcc` (CP/M-80 / Z80) toolchain.

## `build-dcc.py`

Python 3.7+ host builder used by `m.sh` and `m.bat`. It compiles all six C
tools in one parallel wave, links them, and builds the CMake debugger host
and example adapter. Compilation/link errors stop the build with a nonzero
exit status. Build output is grouped in source order.

```sh
python3 scripts/build-dcc.py
python3 scripts/build-dcc.py --jobs 4 --output-path "build/custom objects"
python3 scripts/build-dcc.py --cc clang --no-static
python3 scripts/build-dcc.py --skip-debug-host
```

On Windows use `python` or `m.bat`. MSVC x64/ARM64 setup, Preview installs,
and Visual Studio's bundled CMake are detected automatically. Unix defaults
to GCC on Linux and Clang on macOS; `CC` and `CFLAGS` are respected. C tools
link statically on Linux unless `--no-static` is given. Objects, MSVC listings
and PDBs stay under `--output-path`; final binaries go to the repository root.
`PYTHON` selects the interpreter used by the shell/batch wrappers. `JOBS` sets
the default parallelism; `--jobs` overrides it.

The flag spellings `-OutputPath`, `-CC`, `-NoStatic`, `-VerboseCommands`,
and `-Jobs` are also accepted. `--verbose-commands` prints exact compiler and
linker invocations. Use `--skip-debug-host` to build the six C tools without
the debugger dependencies.

## `run-mir-compiler-mutations.py`

Python 3.7+ runner for the isolated compiler-mutation campaign. It builds and
checks the healthy baseline first, then schedules mutants up to `--jobs` at a
time. Each worker owns separate source, CMake, binary, profile and temp paths.
The mutation anchors and required failure messages live in
`mir-compiler-mutations.json`; no PowerShell is needed by this campaign.

```sh
python scripts/run-mir-compiler-mutations.py --jobs 2 --build-jobs 4
python scripts/run-mir-compiler-mutations.py --names dominance,promotion-cache
python scripts/new-mir-fuzz-source.py --output-path build/replay.c --seed 23117 --programs 1
python scripts/test-mir-fuzz-source.py
```

`--output-directory` defaults to `build/mir-compiler-mutations`, relative to the
repository. `--worker-timeout` bounds startup, builds, tests and pipe draining
(default 2100 seconds); configure/build/probe deadlines remain 120/1800/60
seconds. Legacy `-Jobs`, `-BuildJobs`, `-Names`, `-OutputDirectory`, and
`-WorkerTimeout` spellings are accepted. The output directory is locked against
simultaneous campaigns. `results.json`, per-worker logs and probe artifacts are
retained; temporary source/build trees are removed. A crash, timeout, failed
build, missing/truncated result or unrelated assertion is invalid, never a
mutation kill. Unix signal exits appear as negative Python return codes.

`process_supervision.py` uses Unix sessions and Windows kill-on-close Job Objects,
registers explicitly nested sessions, and bounds inherited-pipe draining.
`test-mir-matcher-restoration.py` and `test-mir-paired-byte-restoration.py` retain
the positive and negative matcher controls. Their PowerShell entry points,
the old mutation worker/module, and the old PowerShell process-supervision
helpers are retired. Clobber workers use the
same Python process supervisor.

## Native compiler proof probes

The following Python 3.7+ entry points share `mir_probes.py` and replace their
PowerShell versions:

- `test-ast-dump.py`: AST diagnostics and retired rollout-control isolation.
- `test-mir-candidate-matrix.py`: all 12 candidate rows across eight probes.
- `test-mir-pointer-condition-mutations.py`: all 85 source mutations.
- `test-mir-scope-block-mutations.py`: four controls and 27 semantic mutations.
- `test-mir-require-emit.py`: strict emission, diagnostics and oversized boundary.
- `run-mir-lifetime-tests.py`: both forced candidates in both stack configurations,
  running the application suite in both optimization modes.

Use `python scripts/<name>.py --dcc /path/to/dcc` to choose a compiler.
`run-mir-lifetime-tests.py --timeout N` controls application timeouts;
`test-mir-require-emit.py --build-dir DIR` retains its diagnostic artifacts.
Child environment overrides leave the caller's environment unchanged.
Named assembly outputs use LF line endings on every host, so strict assembly
SHA-256 baselines compare the same bytes on Windows, macOS, and Linux.

## `run-diagnostics.py`

Python 3.7+ compiler diagnostic runner, shared with `runall.py`. It compares
normalized source paths and line endings against the checked-in baselines.
`warn-*.c` tests require a successful compilation; other tests require failure.
Timeouts, launch failures, and crashes cannot pass validation or update baselines.

```sh
python scripts/run-diagnostics.py --jobs 8
python scripts/run-diagnostics.py --serial
python scripts/run-diagnostics.py --update
```

`--dcc`, `--test-dir`, `--baseline-dir`, `--build-dir`, `--update`, `--serial`,
and `--throttle-limit` retain their previous single-dash aliases. `--jobs`
sets the worker limit (default: logical CPU count); `--timeout` sets the
per-compiler deadline (default: 60 seconds). Relative paths start at the repo
root. Baseline updates are explicit and write UTF-8 without a BOM, using LF.
The separate PowerShell and shell implementations are retired.

## `runall.py`

Native Python 3 runner for the standard build, emulator, baseline, fixture,
and extended-corpus path. It is the regression runner on Windows, macOS, and Linux:
Python uses a substantially cheaper process-launch path than PowerShell on
macOS, which matters because this suite starts thousands of short-lived tools.

```sh
python3 scripts/runall.py --mode full --extended
python3 scripts/runall.py --apps cint,cobint --mode full
```

It supports the normal runner controls, including `--mode`, `--extended`,
`--apps`, `--serial`, `--fail-fast`, `--throttle-limit`, `--timeout`,
`--emulator`, `--build-dir`, `--baseline-dir`, `--keep-build`,
`--no-stack-check`, `--emulated-m80`, `--emulated-l80`, `--narrow-diff`,
`--report`, `--report-file`, `--report-clock-hz`, `--update-perf-baseline`,
`--no-perf-check`, `--timing-breakdown`, and `--no-ram-disk`. The matching
PowerShell-style single-dash spellings (for example `-mode full -extended`)
are also accepted.

Fixtures use `tests/_test_overrides.json`, including explicit `source` paths
and `read_only: true`. They are staged with uppercase CP/M filenames; read-only
permissions apply only to the staged copies, never to the source files.
Repeat staging replaces existing read-only copies before reapplying permissions.
The `tfilero` test also requires ntvcm's read-only open fallback (upstream
commit [`59c0b63`](https://github.com/davidly/ntvcm/commit/59c0b63d5cb1a14b6ee342df9440dee29e67a79f)
or newer); update and rebuild older emulator installations if read-only opens fail.

The exhaustive `tdvbyte` test is built once per mode and runs in seven
divisor batches, checking all 17,432,576 inputs and both division helpers.
Each batch has its own output check; cycle counts are summed for the single app's performance baseline and
report, while the COM size is recorded once. Batch workers share the normal
concurrency limit with other apps, and expensive batched apps start first.
`--serial` / `-Serial` runs the same batches sequentially. Running `tdvbyte`
without arguments still performs the entire exhaustive check in one process.

The `run_batches` override is for read-only test COMs accepting zero-based
`batch count` arguments. All batches must print the same expected summary in
`tests/baselines/<app>_batch.txt`. Fixtures that tests modify cannot be shared
this way. The configured batch count stays fixed across hosts and concurrency
settings so summed cycle baselines remain comparable.

## `typed-expr-fuzz.py`

Differential fuzzer for integer expression code generation. Each generated
program has 40 small functions; each applies one operator to two operands of
chosen types (`signed char` through `unsigned long`) and storage (parameter,
local, nested-block local, global, static, array element, struct member,
pointer, constant) and uses the result in a chosen context (return, `if`,
ternary, compound assignment, converted argument, global store, loop
accumulation). Expected values come from a model of C's conversions with dcc's
type sizes, not from a host compiler, whose 32-bit `int` promotes differently.
Every program is built and run with and without `dccpeep`. `--mode tree`
generates nested expressions instead (binary operators, unary `-` `~` `!`,
casts and `?:` over three typed operands), and `--mode mixed` both.
`--mode store` checks the values of stores instead: `=` then a compound
assignment, a compound assignment's own value, and pre/post `++`/`--`, on a
global, array element, struct member, pointer target, static, local or
nested-block local. `--mode prog` generates small programs instead: several
variables of mixed type and storage, some also reached through pointers, a
run of assignments, compound assignments, `++`/`--`, calls inside
expressions, a call that changes a global, short loops and `if`/`else`,
and a hash of every variable as the result. `--mode scope` adds statements
that open blocks declaring a temporary `t` of a random type (shadowing an
outer `t` when nested), `switch` cases that each declare their own `t` and
may fall through, `do`/`while` loops, forward `goto`s, and byte reads and
writes of wider variables through an `unsigned char` pointer; its programs
have 12 functions. A build or run that goes past `--build-timeout` or
`--timeout` seconds is reported as a hang, and a program too large
to link or to fit in the emulator's memory (usually the unoptimized
build) as skipped.

```sh
python3 scripts/typed-expr-fuzz.py --seeds 1-500
python3 scripts/typed-expr-fuzz.py --seeds 1-500 --mode tree
python3 scripts/typed-expr-fuzz.py --seeds 1-500 --mode store
python3 scripts/typed-expr-fuzz.py --seeds 1-500 --mode prog
python3 scripts/typed-expr-fuzz.py --seeds 1-500 --mode scope
python3 scripts/typed-expr-fuzz.py --seeds 1-200 --env DCC_MIR_SELECT_CANDIDATE=homed-lazy
python3 scripts/typed-expr-fuzz.py --generate 7 prog.c prog.expected
```

A failing seed keeps `t.c`, `t.expected` and each mode's output under
`build/typed-expr-fuzz/<seed>`; an output line is the case number times ten
plus the input index, then the result as an `unsigned long`, both in hex.

## `clang-oracle-fuzz.py`

Differential fuzzer whose expected output comes from the host C compiler. It
complements `typed-expr-fuzz.py`: that one checks single expressions against a
model; this one generates whole programs and lets clang say what they print.

Each seed is one program of helper and test functions over fixed-width
integers, arrays, structs, bit-fields and pointers, with `for`/`while`/`do`
loops, `switch`, nested-block locals, `goto`, calls with mixed-width
arguments, bounded recursion, calls through function pointers, compound
assignment and `++`/`--` on every kind of lvalue, and assignments used as
values. The generator and renderers are in `clang_oracle_gen.py`.

`--gen float` switches to `clang_oracle_float_gen.py`, which builds on the
same model to exercise dcc's only floating type: `float` globals, locals,
parameters, arrays and struct members; arithmetic, negation, comparison,
truth tests, `?:`, `++`/`--`, compound assignment, conversions to and from
every integer width, and mixed integer/float arithmetic. Every operation is
clamped so no value overflows, and floats are printed and checksummed as
their IEEE bits with `-0` folded to `0`, so a difference is a difference in
the arithmetic rather than in formatting. Its failures and reductions go
under `build/clang-oracle-fuzz-float/`.

The same source is compiled by clang and by dcc. It can be, because it never
spells `int` or `long`, and the renderer inserts the casts and masks that
make an expression mean the same with a 16-bit and a 32-bit `int`. What a
cast cannot fix (signed overflow at 16 bits, division by zero, and so on) is
caught at run time: a second, host-only rendering routes every such operation
through a checking helper, and the driver repairs the operations that trip,
or drops the program. A program is used only when

- the checked build, under ASan and UBSan, reports nothing;
- clang finds no unsequenced modification, no missing `return` and no syntax
  that is only valid from C23; and
- `-O0`, `-O2`, the sanitized build and the checked build all print the same.

So a difference from dcc is a dcc defect, not a porting difference.

```sh
python3 scripts/clang-oracle-fuzz.py --seeds 1-300 --off known
python3 scripts/clang-oracle-fuzz.py --seeds 1-300 --off known --configs all
python3 scripts/clang-oracle-fuzz.py --reduce 17,42 --config nopeep --off known
python3 scripts/clang-oracle-fuzz.py --generate 17 prog.c --off known
python3 scripts/clang-oracle-fuzz.py --list-configs
python3 scripts/clang-oracle-fuzz.py --gen float --seeds 1-300 --off known
python3 scripts/clang-oracle-fuzz.py --gen float --reduce 36 --config nopeep --off known
```

- `--configs production` builds with and without `dccpeep` and with and
  without the stack check. `default` adds `DCC_MIR_NO_EXACT=1`. `all` adds one
  config per MIR candidate, forced with `DCC_MIR_SELECT_CANDIDATE` (or
  `DCC_MIR_PREFER_REGIONAL=1` for the regional one); a failure only there is
  latent, not shipped behaviour.
- `--off known` leaves out every form that hits a bug already logged in
  `compiler-bugs.md`, so a run reports new failures instead of the same ones
  again. `KNOWN_BUGS` in `clang_oracle_gen.py` (and in
  `clang_oracle_float_gen.py` for `--gen float`) maps each such form to its
  entry; delete a line when its bug is fixed. The float generator also
  rewrites a few logged shapes after generation (`Gen.steer`) when they are
  easier to see in the finished tree, for example by swapping the operands
  of a `+` whose right side is a product. `--off` also takes individual
  generator features (`-h` lists them) to narrow a search by hand. A program
  whose undefined-behaviour repair rebuilt one of those forms is dropped and
  counted as `INVALID`.
- Failures are reported as `COMPILE` (dcc rejects the program), `VERIFY` (dcc
  prints a MIR verifier diagnostic but still exits 0), `MISMATCH`, `RUN-HANG`
  or `BUILD-HANG`. A program that only overflows its stack, does not link
  into 64K or reports `out of ram` at start-up has met a limit of the target,
  not a defect; it is counted as `limit-only`. A failing seed keeps `t.c`,
  `t.expected` and each failing
  config's output under `build/clang-oracle-fuzz/<seed>`. An output line is a
  call number and the returned value, or 0x4000 plus the call number and a
  checksum of every global after that call.
- `--reduce` shrinks a failing seed to a small program that still fails the
  same way and is still accepted by the host checks, written to
  `build/clang-oracle-fuzz/<seed>-reduce-<config>/reduced.c`. It refuses
  edits that would turn the failure into a known one. Reduce a hang with a
  short `--timeout`.

A seed names a program only together with the generator version and
`--tests`, `--size`, `--depth` and `--off`; keep the reduced program, not the
seed.

## `run-known-bugs.py`

Checks the reproducers parked in `tests/known-bugs/`, one per open entry in
`compiler-bugs.md`. For each it proves the baseline with the host compiler
(same output, clean under ASan and UBSan, no unsequenced modification), then
builds and runs the program with dcc with and without `dccpeep` and reports
`fails` with the reason, or `FIXED`. See `tests/known-bugs/README.md` for what
to do with a fixed one.

```sh
python3 scripts/run-known-bugs.py
python3 scripts/run-known-bugs.py bug003 --dcc path/to/another/dcc
python3 scripts/run-known-bugs.py --update bug022    # write a new baseline from clang
```

## `bug-issue-body.py`

Writes the GitHub issue for an entry in `compiler-bugs.md`: the entry's text,
the reproducer from `tests/known-bugs/`, the build commands, the host
compiler's output (checked against the baseline) and what dcc does now, with
and without `dccpeep`. The title comes from the entry's summary-table row and
cross-references become issue links through the table's Issue column, so the
row must exist first. Bodies go to `build/bug-issues/bugNNN.md`; `--create`
opens the issue with `gh`.

```sh
python3 scripts/bug-issue-body.py bug054
python3 scripts/bug-issue-body.py bug054 --create
```

## `mir-migration-census.py`

Measures generated MIR selection across `tests/*.c` and compares two snapshots
to produce the smallest `runall.py --apps ...` validation command for a
compiler change.

The script is read-only with respect to test and performance baselines. It
compiles each source with `DCC_MIR_SELECT_REPORT=1` and
`DCC_MIR_REQUIRE_EMIT=1`, deduplicates buffered/final reports by
`(app, function)`, and writes a stable tab-separated snapshot containing the
selected generated selector, assembly-text size, instruction count, hash, and
CFG block count. Compatibility `captured_*` columns remain present with `-1`;
no legacy text is retained or measured.

### Fast staged MIR workflow

```sh
# 1. Snapshot before changing a selector or MIR optimization.
python3 scripts/mir-migration-census.py \
  --output build/mir-before.tsv

# 2. Edit and rebuild dcc.
sh src/dcc/build-dcc.sh

# 3. Measure the new rollout and print newly accepted/regressed functions plus
#    the exact focused validation command for affected apps.
python3 scripts/mir-migration-census.py \
  --output build/mir-after.tsv \
  --compare build/mir-before.tsv \
  --fail-on-regression

# 4. Run the printed command, for example:
python3 ./scripts/runall.py --apps tret,tatexit --mode full --timeout 20
```

Use `--apps app1,app2` while developing a local change. Run the complete census
only when the focused hypothesis succeeds. Reserve an unfiltered
`runall.py --mode full` for a material coverage milestone instead of every
selector iteration.

Apps marked `ignore` in `tests/_test_overrides.json` are skipped by default, so
the complete census matches the runnable app suite. Pass `--include-ignored`
when deliberately investigating those sources. Per-app `dcc_args` overrides
are forwarded automatically.

`--fail-on-regression` returns nonzero when a previously reported MIR selection
disappears or becomes non-MIR. Run the generated focused command whenever a
selected hash or generated metric changes.

### MIR-only cost-policy matrix

`--cost-policy-output` records the generated MIR candidates considered by the
`mir-v1` policy. The TSV includes emitted machine bytes/instructions,
loop-weighted T-states, helper calls, frame/spill costs, allocator and stream
moves, prologue/callee-save costs, register homes, eligibility, score, and
output hash. With no explicit `--cost-policy`, the option uses the production
`mir-v1` default; pass `--cost-policy mir-v1-report` to report without adopting
alternatives.

```sh
python3 scripts/mir-migration-census.py \
  --cost-policy mir-v1 \
  --cost-policy-output build/mir-cost.tsv \
  --output build/mir-selected.tsv
```

## `mir-current-vs-parent.py`

Runs strict normal and stack-check censuses with a current compiler and a
separately built parent compiler, then reports selection/hash changes:

```sh
python3 scripts/mir-current-vs-parent.py \
  --parent-compiler build/parent/dcc \
  --apps cint,cobint
```

This replaces forced-legacy A/B and fallback-bisection utilities.

For one generated-candidate runtime comparison, use the diagnostic controls:

```sh
DCC_MIR_SELECT_FUNCTION=parse_move \
DCC_MIR_SELECT_CANDIDATE=spilled-rhs-forward \
  python3 ./scripts/runall.py --apps tchess --mode full
```

Candidate names are the `candidate` values in `--cost-policy-output`; no
legacy stream is involved.

## `run-mir-lifetime-tests.py`

Runs the physical-lifetime candidates that require diagnostic forcing in both
peep/nopeep and stack/no-stack modes. It verifies that `regional_address`
selects `regional` before exercising DE preservation in `tmirlife`, and that
`cint.primary` selects `spilled-phi-slot` before running both CINT workloads.
It also requires the same-ABI signed-byte scanner near-match to reach and
decline the bounded-decimal schedule's argument-conversion guard.

```sh
python scripts/run-mir-lifetime-tests.py
```

## `run-mir-clobber-tests.py`

Runs the MIR emission-clobber and exact-schedule near-match regressions in
peep/nopeep and stack/no-stack modes. It covers argument-staging register
clobbers, exact-call ABI, call-identity and semantic-payload guards, IY
ownership, regional validation, and stack-checked VLA reductions.
The IY coverage includes separately peepholed caller/callee modules, so
callee-save preservation is tested across assembly-file boundaries.

```sh
python scripts/run-mir-clobber-tests.py
```

The runner uses Python 3.7+ and the shared `process_supervision.py` helper.
Historical built-in cases and forced configurations live in
`mir-clobber-builtins.json`; additional campaigns remain in `mir-clobber-cases/`.
Case aliases keep their original membership. Every worker's completed manifest
must match its expected leaves, and the parent checks their exact union.

```sh
python scripts/run-mir-clobber-tests.py --jobs 8 --cases domloop,semantics
python scripts/run-mir-clobber-tests.py --list-executions build/clobber-expected.json
python scripts/run-mir-clobber-tests.py --jobs 8 --execution-manifest build/clobber-executed.json
```

`--cases`, `--fuzz-seeds`, `--run-timeout`, `--build-timeout`, `--jobs`,
`--shard-index`, `--shard-count`, `--list-executions`, and
`--execution-manifest` also accept their previous single-dash spellings.
Listing requires no compiler, emulator, or generator. Failed runs retain their
source files, build/run logs, and shard artifacts under `build/`.

## `publish-package.ps1`

Publishes or republishes the binary package release. By default it reads the
version from `scripts/package-version.txt` (`v2.0.0`), deletes any existing
GitHub release/tag for that version, recreates the tag at the current commit,
and pushes the tag so `.github/workflows/release.yml` rebuilds the package
assets.

```pwsh
pwsh ./scripts/publish-package.ps1
pwsh ./scripts/publish-package.ps1 -Version v2.0.1 -Watch
```

The script requires `git` and the GitHub CLI (`gh`) on `PATH`, and refuses to
publish from a dirty worktree unless `-AllowDirty` is passed.

## Host compilers to install

The Python build and test drivers support Python 3.7+ on Windows, macOS,
and Linux. Use the normal native compiler for each platform:

| Platform | Recommended compiler | Install notes |
| -------- | -------------------- | ------------- |
| Windows | MSVC x64 | Install Visual Studio 2022 or Visual Studio Build Tools with **Desktop development with C++**. |
| Windows ARM64 | MSVC ARM64 | Install Visual Studio Build Tools with **Desktop development with C++** plus the **MSVC ARM64/ARM64EC build tools** component. |
| macOS | Apple clang | Install Xcode Command Line Tools with `xcode-select --install`. |
| Linux | GCC | Install your distribution's C/C++ build tools. |

`build-dcc.py` and `validate-unit-test.py` follow those defaults: MSVC on
Windows, native ARM64 MSVC on Windows ARM64, clang on macOS, and gcc on Linux.
On Unix-like hosts, pass `-CC` or set `CC` when you intentionally want a
different compiler.

On macOS, host validation recompiles every selected app but preserves the
existing executable when the new binary is byte-identical. This avoids the
substantial first-launch cost for newly linked executables on repeated runs.
Every app still executes and its output is compared with the baseline; changed
binaries replace the previous executable, and failed builds fail validation.
Keep `build/host-validate` between runs to benefit from this optimization.

### Linux 32-bit GCC support for baseline validation

32-bit/multilib support is only useful for `validate-unit-test.py`; it is not
needed to build `dcc`, `dccpeep`, or `dccrtlstrip`. The host tools should be
built with the normal native compiler for the platform.

The validator can get closer to dcc's target model on Linux if GCC can build and
link 32-bit executables. When running on Linux with GCC, it probes `gcc -m32`; if
the probe succeeds, it adds `-m32` to host test builds and prints `(-m32)` in the
compiler summary. If the probe fails, validation continues with normal
host-width GCC.

Common package installs:

```sh
# Debian / Ubuntu
sudo apt update
sudo apt install build-essential gcc-multilib g++-multilib

# Fedora
sudo dnf groupinstall "Development Tools"
sudo dnf install glibc-devel.i686 libgcc.i686

# openSUSE
sudo zypper install -t pattern devel_C_C++
sudo zypper install gcc-32bit glibc-devel-32bit

# Arch Linux
sudo pacman -S base-devel gcc-multilib
```

The 32-bit host compiler mainly helps validation tests that depend on 32-bit
`long` width. It does not fully emulate dcc: dcc still has 16-bit `int`, 16-bit
pointers, CP/M file records, BDOS/BIOS calls, Z80 port I/O, and dcc-specific
stack-check runtime behavior. Tests that depend on those semantics should remain
marked with `"host": true` in `tests/_test_overrides.json` or skipped by the
validator's explicit CP/M/Z80 source checks.

## `stacksize.sh` / `stacksize.bat`

Finds the minimum **C stack reserve** an app needs under dcc's lightweight
stack-overflow guard (`-fstack-check`).

`stacksize.sh` is the macOS/Linux version; `stacksize.bat` is the equivalent for
Windows (`cmd.exe`). They take the same arguments, honour the same environment
variables, and produce the same report — the Windows version drives `ma.bat`
instead of `ma.sh`.

### Purpose

On the Z80/CP/M target the C stack and the `malloc` heap share memory with no
hardware protection. If a program recurses too deeply (or is built with too
small a `-stack` reserve) the stack can grow down into the heap and silently
corrupt it. The `-fstack-check` guard turns that silent corruption into a clean
`?stack overflow` message and exit.

`stacksize.sh` uses that guard to **measure** how much stack an app actually
needs: it builds the app with the guard forced on, runs it, and sweeps the
`-stack` reserve upward until the program runs without tripping the guard. The
first size that runs clean is the minimum requirement; the script also prints a
rounded-up recommendation with a little headroom.

### Usage

```sh
scripts/stacksize.sh <app> [-- emulator-args...]
```

- `<app>` — test/app name as passed to `ma.sh` (e.g. `triangle`, `cobint`).
- `-- args...` — arguments to pass to the emulated program (e.g. a data file
  such as `e.cob`). Everything after `--` is forwarded to the emulator.

Run it from the repo root (or anywhere — it locates the repo root relative to
the script). The in-repo `./dcc`, `./dccpeep`, `./dccrtlstrip` binaries and the
`ntvcm` emulator must be available.

### Examples

```sh
scripts/stacksize.sh triangle          # simple app
scripts/stacksize.sh cobint -- e.cob   # app that needs a data-file argument
```

Sample output:

```
Finding minimum stack for 'triangle' (guard on): start=256 step=64 max=8192 mode=peep

  stack=256    : overflow
  stack=320    : overflow
  ...
  stack=640    : OK

Minimum passing stack reserve : 640 bytes
Recommended (with headroom)   : 768 bytes

Build it with:  DCC_STACK_SIZE=768 ./ma.sh triangle peep
Or compile direct:  ./dcc -fstack-check -stack 768 tests/triangle.c -o TRIANGLE.mac
```

### Options (environment variables)

| Variable | Default | Meaning |
| -------- | ------- | ------- |
| `START`  | `256`   | First stack size to try (bytes). |
| `STEP`   | `64`    | Increment between tries (bytes). |
| `MAX`    | `8192`  | Give up after this size. Prevents an infinite loop on apps that overflow on purpose (e.g. `spsmash`). |
| `MODE`   | `peep`  | Build mode passed to `ma.sh` (`peep` or `nopeep`). |
| `EMU`    | `ntvcm` | Emulator command used to run the `.COM`. |

```sh
START=512 STEP=128 scripts/stacksize.sh triangle
MAX=16384 scripts/stacksize.sh somedeepapp
```

### Exit status

- `0` — a passing stack size was found (printed as the minimum + recommendation).
- `1` — no passing size up to `MAX`. The app may overflow on purpose (deliberate
  unbounded recursion, like `spsmash`), or it genuinely needs more than `MAX`
  bytes — raise `MAX=...` and retry.

### How it ties into the build

The sweep varies the reserve through the `DCC_STACK_SIZE` hook honored by
`ma.sh`, and forces the guard on with `DCC_FORCE_STACK_CHECK=1`. Once you know
the size, bake it in by building with `DCC_STACK_SIZE=<n> ./ma.sh <app>` or, for
the regression suite, set the app's `stack_size` in
`tests/_test_overrides.json`, which is read by `runall.py`.

## `dccprof.py`

Builds an app (peep-optimized, the real shipped build), runs it under
`ntvcm`'s per-PC execution-count profiler (`-g:<file>`), and correlates the
result against the build's `.PRN`/`.SYM` listings into a hot-function
summary plus per-line annotated listings you can open directly in an
editor - no manual address correlation required.

### Purpose

`ntvcm -g:<file>` writes a raw `pc,count,asm` CSV of every executed
address, but a dcc build links two separately-assembled modules (the app
and the stripped runtime, `RTLMIN.MAC`) at final addresses that differ from
each module's own standalone `.PRN` listing by a different offset per
module - and a `.PRN` listing's address column is the address *after* each
line's own emitted bytes, not its start (see `dccprof.py`'s own module
docstring for how this was confirmed against known Z80 instruction
encodings). Getting either of these wrong silently shifts hit counts to
the wrong function or line with no crash to reveal the mistake.
`dccprof.py` formalizes the correlation once, correctly, instead of
requiring it be re-derived by hand for every profiling investigation - it
is also directly reusable on its own against an already-built app and an
already-captured profile.

### Usage

```sh
python scripts/dccprof.py <app> [-SourcePath FILE] [-BuildDir DIR] [-OutDir DIR] [-Clock HZ] [-ProgramArgs ...]
```

One Python 3.7+ script supports Windows, macOS, and Linux. It delegates
the optimized build to `ma.py` and adds the profiling-specific steps: regenerating
`RTLMIN.PRN` (a normal build only assembles it without the `/L` listing
flag, since nothing else needs it), running the app under `ntvcm -g`, and
generating the annotated reports.

### Examples

```sh
python scripts/dccprof.py tbig
python scripts/dccprof.py tbig -ProgramArgs 20000
python scripts/dccprof.py mm -BuildDir /tmp/profmm
```

### Output

Written to `-OutDir` (default: same as `-BuildDir`):

- `<app>_profile_summary.md` — ranked hot-function table (hits, % of
  total, module, function name). Open directly, or in VS Code's Markdown
  preview.
- `<app>_profile_app.txt` — the app's own `.MAC`, with every instruction
  line prefixed by its own hit count, in original `.PRN` address/line-
  number order.
- `<app>_profile_rtl.txt` — the same, for whichever `DCCRTL.MAC` routines
  were actually hit (routines never reached are omitted - the full runtime
  is large and a given run typically touches only a small slice of it).

Open an annotated listing directly in any editor and use search / go-to-
line to jump to a specific hot address or line called out in the summary.

### Analyzing an existing profile

Given an already-built app (its `.PRN`/`.SYM`/`.MAC`, plus a regenerated
`RTLMIN.PRN`) and an already-captured profile CSV, skip the build+run
steps entirely:

```sh
python3 scripts/dccprof.py --app tbig --build-dir build/dccprof/tbig --profile-csv build/dccprof/tbig/tbig_profile.csv
```

### Environment Variables

| Variable | Default | Meaning |
| -------- | ------- | ------- |
| `M80C` | `m80c` | Native assembler used to regenerate `RTLMIN.PRN` |
| `NTVCM` | `ntvcm` | Emulator command |

The build and correlation subprocesses use the same Python interpreter as the
entry point. `ma.py` tool and asset overrides also apply. Use `--` before app
arguments (including flags), or `--program-args`/`-ProgramArgs`.

## `ma.py`

Python 3.7+ single-app build driver, launched by root `ma.sh` and `ma.bat`.
The `scripts/ma.sh` compatibility wrapper also launches it. Set `PYTHON` to
select another interpreter. The driver uses native `dccmake` for compilation,
peephole optimization, runtime stripping, assembly, and linking.

```text
python scripts/ma.py sieve
python scripts/ma.py e nopeep
python scripts/ma.py ttt full --build-dir build/ttt
python scripts/ma.py e --emulated-m80 --emulated-l80
```

The default mode is `fast` (`peep` is an alias). `full` builds fast then
nopeep, leaving the unoptimized artifact last. Sources are found in `tests/`
then the current directory; use a path or `--source-path FILE` to select one.
`--stack-size N` overrides `DCC_STACK_SIZE`. Float/long formatted I/O is
inferred from the source or enabled with `DCC_FLOATIO=1`/`DCC_LONGIO=1`.

Tool overrides (`DCCMAKE`, `DCC`, `DCCPEEP`, `DCCRTLSTRIP`, `M80C`, `L80C`,
`NTVCM`, `M80`, `L80`), asset paths (`DCC_HOME`, `DCC_LIB`, `DCC_INCLUDE`,
`DCC_RUNTIME`), `DCC_ARGS`, `DCC_FORCE_STACK_CHECK`, and
`DCC_STRIP_UNUSED=0` are supported. `NTVCM_ARGS` supplies whitespace-separated
emulator flags for emulated assembly/linking. Standalone builds ignore a
nearby `dccmake.txt`; invoke `dccmake` directly for project builds.
Run `python scripts/ma.py --help` for all options.

## `compiler-coverage.sh`

Builds a separate Clang-instrumented `dcc`, runs the main suite and the C89,
C99, and C11 extended tests in both optimization modes, and produces LLVM line
and branch coverage reports. The instrumented executable and reports stay under
`build/compiler-coverage/`; the normal `./dcc` binary is not replaced.

```sh
sh scripts/compiler-coverage.sh
```

The text summary is written to
`build/compiler-coverage/report/summary.txt`, and the browsable report starts
at `build/compiler-coverage/report/html/index.html`. Set `CC`,
`LLVM_COV`, or `LLVM_PROFDATA` to override tool discovery.
Coverage clobber builds have a separate 300-second timeout because
instrumentation and cache verification can exceed the ordinary 60-second
build budget. Set `DCC_COVERAGE_CLOBBER_BUILD_TIMEOUT` to override it. Standalone
`run-mir-clobber-tests.py` accepts `--build-timeout` (default 60); `--timeout`
continues to control target execution independently.

## `prepush.py`

Run the complete local pre-push gate with one command:

```sh
python scripts/prepush.py
```

It runs the build-diagnostic regression checks, native compiler baseline validation
(`validate-unit-test.py`), then the comprehensive MIR proof suite described below.
The entry points and native proof probes use Python 3.7+. The final coverage
workflow uses the Python clobber runner and still requires sh and matching LLVM
coverage tools. PowerShell is not required. `--list` needs only Python.
Native validation honors the host-only exclusions in `tests/_test_overrides.json`
and uses `-Jobs` and `--timeout` for its worker and timeout limits.
The MIR proof suite includes strict `--mode full -Extended`
release suites with and without stack checks, plus builds, script tests,
audits, host and sanitizer tests, debugger tests, compiler mutants, and coverage.
It stops on failure and returns a nonzero exit code. It does not commit or push.

Use `-List` to inspect the gates. `-Jobs`, `-MutationJobs`, `-MutationBuildJobs`,
`--timeout` (default 60 seconds per target), `-OutputDirectory`, and
`-RequireComplete` are forwarded to the proof suite. Paths are resolved from
the repository root, even when invoked from another directory. This is a long
run and needs the proof suite's compiler, emulator, and coverage prerequisites.
The proof suite retains its artifacts and receipt under its output directory.
The early Python phase compiles the data-driven positive exact-schedule controls
and checks campaign assembly baselines and type-sensitive symbol mutations, so
these regressions are reported before the long coverage campaign.
Automatic LLVM discovery prefers the newest installed `clang-N` over the
unversioned `clang` command and resolves matching coverage tools. Set `CC`
explicitly to choose a different toolchain (for example,
`CC=clang-18 python scripts/prepush.py` in a POSIX shell).

## `run-mir-proof-suite.py`

Runs every maintained AST/MIR proof gate through one phase-gated command:
canonical and independent builds, script/static audits, normal and sanitized
host tests, debugger-host tests, isolated compiler mutants, strict stack and
no-stack release suites, the extended MIR census, and the complete instrumented
coverage workflow.

```sh
python scripts/run-mir-proof-suite.py
```

The Python 3.7+ runner preserves all 11 phases, LLVM companion version checks,
bounded preparation/release groups, and the `receipt.json` phase timestamps.
`--list` works without PowerShell or other build tools. `--jobs`,
`--mutation-jobs`, `--mutation-build-jobs`, `--timeout`, `--output-directory`,
`--require-complete`, and `--all` have the original PowerShell-style aliases.
The final coverage workflow still requires sh and matching LLVM tools; its
clobber suite and Python campaign callers do not launch PowerShell.

The runner uses all detected processors by default and retains each run below
a unique `build/mir-proof-suite-*` directory. Use `-List` to inspect the
ordered gates without executing them, or set `-Jobs`, `-MutationJobs`, and
`-MutationBuildJobs` to control the combined CPU budget. Independent script,
CMake, host, sanitizer, and debugger preparations run concurrently; the two
strict release modes split the available workers. Mutation and coverage phases
retain their own bounded schedulers. A complete run is intentionally long:
strict native release gates and instrumented coverage execute separately so
coverage instrumentation cannot substitute for release evidence.
Ambient `DCC`/`DCC_*` diagnostic, selector, mutation, and driver controls are
cleared before execution; installation paths such as `DCC_HOME`,
`DCC_INCLUDE`, `DCC_LIB`, and `DCC_RUNTIME` are preserved. The sanitizer build
explicitly uses the same discovered or configured Clang toolchain as coverage.
Run this proof suite periodically on a developer machine; GitHub CI intentionally
runs only the standard cross-platform `runall.py --mode full` regression gate.

## `runall.py`

Runs the standard application suite with baseline, fixture, diagnostic, and
performance checks. Use Python 3 on Windows, macOS, and Linux.

```sh
python3 scripts/runall.py --mode full --extended
python3 scripts/runall.py --apps app1,app2 --mode full
python3 scripts/runall.py --mode full --no-stack-check --timeout 30 --failures-only
```

Run `python3 scripts/runall.py --help` for the complete option list. It accepts
`--mode` (`fast`, `nopeep`, or `full`), `--extended`, `--apps`, `--serial`,
`--fail-fast`, `--throttle-limit`, `--timeout`, `--emulator`, `--build-dir`,
`--baseline-dir`, `--keep-build`, `--no-stack-check`, `--emulated-m80`,
`--emulated-l80`, `--narrow-diff`, `--report`, `--report-file`,
`--report-clock-hz`, `--update-perf-baseline`, `--no-perf-check`, and
`--timing-breakdown`.

The runner reads `tests/_test_overrides.json`; fixture files are copied into the
run directory before execution. Build artifacts are removed after a successful
run unless `--keep-build` is passed.

## `stress-runall.ps1`

Stress-tests the suite by running `runall.py` repeatedly, stopping on the first
failure. Useful for shaking out intermittent/flaky failures under parallel load.
The failing iteration's full log is kept for inspection.

### Usage

```pwsh
pwsh ./scripts/stress-runall.ps1                # up to 50 parallel runs
pwsh ./scripts/stress-runall.ps1 -Iterations 100
pwsh ./scripts/stress-runall.ps1 -Serial -Iterations 10
pwsh ./scripts/stress-runall.ps1 -ThrottleLimit 8 -KeepLogs
```

### Parameters

| Parameter | Default | Meaning |
| --------- | ------- | ------- |
| `-Iterations` | `50` | Maximum number of suite runs |
| `-Serial` | (off) | Run the suite serially each iteration |
| `-ThrottleLimit` | CPU core count | Max concurrent apps in parallel mode |
| `-LogDir` | temp folder | Where per-iteration logs are written |
| `-KeepLogs` | (off) | Keep all logs, not just the failing one |

### Exit Status

- `0` — All iterations passed
- `1` — An iteration failed (loop stopped early)

## `convert-baseline.ps1`

Splits the legacy concatenated baseline `baseline_test_dcc.txt` into per-app
baseline files under `tests/baselines/` (one `<app>.txt` per test). Used to
(re)generate the baselines consumed by `runall.py`.

### Usage

```pwsh
pwsh ./scripts/convert-baseline.ps1 -AppList (Get-Content legacy-app-order.txt)
```

### Parameters

| Parameter | Default | Meaning |
| --------- | ------- | ------- |
| `-InputFile` | `baseline_test_dcc.txt` | Legacy concatenated baseline |
| `-OutputDir` | `tests/baselines` | Destination for per-app baseline files |
| `-AppList` | required | Complete original app order used as split boundaries |

Supply the complete original app order in `legacy-app-order.txt`, one app
name per line. The converter uses that order so output lines beginning with `test ` (e.g.
`test tstdc completed with great success`) are not mistaken for section headers.
The split reproduces the original baseline byte-for-byte.
