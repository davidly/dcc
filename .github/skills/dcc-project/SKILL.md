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
| `src/dccpeep/` | Fixpoint peephole optimizer (`-Ot` time / `-Os` size). `dccpeep.c` owns the descriptor-driven scheduler and remaining general passes; `PeepContext` groups options, statistics, mutation versions, and indexes. `peep_lines.c` owns the mutable line program, opaque user-asm barriers, and edit transactions; `peep_parse.c`, `peep_effects.c`, `peep_control_flow.c`, and `peep_analyze.c` provide parsing, cached effects, indexed control flow, and safety analysis. Pass families live in `peep_pass_once.c` (micro-pattern dispatcher), `peep_pass_minmax.c` (board/game idioms), `peep_pass_loops.c` (loop registerization), `peep_pass_inline_temp.c` (compiler-tagged spills), and `peep_pass_control_flow.c` (label/branch rewrites); `peep_pass_stubs.c` and `peep_pass_final.c` own post-convergence size and cleanup passes. |
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
- `perf_ignore`: exclude an app from cycle-count comparison. Some apps are not
	cycle-deterministic even with a byte-identical `.COM` - `tkbd` varies by
	several percent between runs - so a "regression" there means nothing. When
	comparing two builds by hand, exclude these before drawing conclusions.

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

## Register allocation

dcc allocates physical registers speculatively: pick a candidate, generate the
whole body with it promoted, verify the emitted text, then commit or rewind.
`dcc_regalloc.c` owns the whole-function attempts, `dcc_loop_regalloc.c` the
loop-scoped ones, and `dcc_func.c` the candidate searches. `Sym.reg_alloc`
carries the result: `REG_BC`, `REG_E` or `REG_IY`.

**Which register, and why.** BC and E are caller-saved, so a function that
contains a call cannot use them at all. IY is callee-saved and is therefore the
only register available in such functions - which is most of them. That rests on
two facts, both load-bearing:

1. Every dcc function claiming IY pushes the caller's IY ahead of its frame and
	pops it after restoring IX. `frame_first_param_offset()` accounts for the
	word this occupies by shifting every parameter by 2.
2. Nothing else in a linked image writes IY. `DCCRTL.MAC` contains no IY
	instruction, and CP/M 2.2's BDOS is 8080 code with no index registers. Run
	`python3 scripts/rtl-iy-safety.py` after any runtime edit; it exits non-zero
	if the invariant breaks.

**Ownership is published, not inferred.** dcc emits
`;@dcc.reg claim=<reg> scope=... sym=... kind=... val=<cycles>` and a matching
`;@dcc.reg free=<reg>` where the live range ends. dccpeep reads these as
intervals (`bc_regalloc_claimed_in_range` / `_from`), so a loop-scoped claim
stops forfeiting the register for the rest of the function. A pass must ask
about the span it actually intends to modify - an unclaimed start no longer
implies an unclaimed remainder. `peep_reg_used_in_function()` is the shared
"is this register spoken for here" scan.

**dccpeep's IY passes are not callee-saved.** If dcc has claimed IY anywhere in
the file, they must stand down (`dcc_iy_claimed_in_file`). File scope, not
function scope: the hazard is a *callee* borrowing IY and destroying its
caller's promoted value. This was a real miscompile on `wumpus.c`.

**Cost model.** References are weighted by loop nesting (8 per level, tracked in
`scan_function_body_ident_counts`) and converted to cycles by
`regalloc_estimate_value`. Record the value at the decision point and publish
that same number - do not recompute it at the emission site, or the claim will
advertise something other than what was decided.

Hard-won rules, each of which cost a measured regression to learn:

- **Verify against the corpus, not against intuition.** Every plausible-sounding
	arbitration improvement here measured at exactly zero. A census of declined
	IY candidates attributed 1035 to value, 257 to non-word types, 96 each to
	written and char parameters, 13 to address-taken, and **zero to register
	contention**. Candidate *supply* is the constraint, not arbitration. Measure
	where the losses are before building machinery.
- **Do not add a `reg_alloc` arm to `gen_deref_addr_ast`'s plain-identifier
	path.** Promoting a dereferenced pointer defeats dccpeep's cross-iteration
	hoisting of the invariant pointer reload, which is worth more. `p->field` via
	`gen_member_addr_ast` is safe and necessary; `*p` and `p[i]` are not.
- **Loop weighting must respect unbraced bodies.** `for (...) if (c) { ... }`
	will attribute the `if`'s brace to the loop unless the scan consumes the loop
	header and only accepts a `{` as the body when it is the first token after
	the closing `)`. That defect scored a parameter at 65 from two references and
	cost 1.1M cycles.
- **Written parameters are eligible for IY but not for BC.** BC's read-only bar
	exists because `regalloc_buffer_finalize`'s reload-repair treats the frame
	slot as a valid shadow. IY needs no repair, so the slot is simply dead and no
	spill is required. `inc iy` is 10 T-states against roughly 82 for the frame
	read-modify-write - the largest per-reference saving available.
- **Exclude functions containing a VLA.** They manage SP through per-scope
	`#vlasp` slots rather than purely `ld sp,ix`; a callee-save push on top of
	that is not worth the risk, and measured as a loss.
- **`buf_has_foreign_iy_use` asks whether anything WRITES IY**, not whether text
	matches. Indexed accesses `(iy+d)` only read it. `inc iy`/`dec iy` are dcc's
	own. An exact push/pop count guard rejects every real candidate - do not add
	one.
- **`current_function_has_call` is not reliable at speculative-attempt time.**
	Inline substitution saves and restores it around a callee, leaving it holding
	that callee's value. Derive per-function facts in
	`scan_function_body_ident_counts` instead.
- **Do not promote LOCALS to IY on reference count.** This was tried in full and
	reverted: net +5.0M cycles. A parameter always arrives in memory, so the
	"38 T-states becomes 25" model holds. A short-lived local whose live range
	fits in one basic block is already kept in HL or A by dccpeep and never
	touches the frame, so promoting it manufactures push/pop traffic. `trw`'s
	`check_buf` lost 14.8% of the whole application this way. Raising the local
	threshold does not separate them - `fint`'s `next()` has a local scoring
	three times the bar that still loses 5.2M. The needed discriminator is
	liveness across basic blocks, which the token scan cannot supply; a retry
	must give the candidate search a real CFG first.
- **Know what IY is worth before planning around it.** Reading it costs
	`push iy` / `pop hl` = 25 T-states against 38 for a frame word, so only 13 are
	saved. BC is `ld l,c` / `ld h,b` = 8, saving 30 - more than twice as much.
	`(iy+d)` is 19, identical to `(ix+d)`, so there is no gain in using IY merely
	as a second base pointer.
- **Widening candidate supply has failed three times running. Stop proposing
	it without first improving the model.** Locals-by-reference-count (+5.0M),
	relaxing the loop scan's control-exit gate so `return`/`break` no longer
	disqualify a loop (+10.8M, tchess +3.0%), and adding best-value arbitration
	between loop-BC and whole-function IY (+9,943, neutral) were each implemented
	fully, measured, and reverted.

	The common cause is that the cost model prices a promotion against an assumed
	*memory* baseline, when the real baseline is whatever dccpeep would otherwise
	have done - which is frequently a register already. Every candidate admitted
	on that basis is a coin flip. Until the compiler can see cross-block liveness
	and model what the peephole would do with a value, admitting more candidates
	loses more than it wins. The existing tight gates are load-bearing, not
	timidity.
- **A machine-level allocator in dccpeep was analysed and the rewrite phase was
	falsified.** `peep_frame_alloc.c` is the retained analysis-only result. It
	treats `(ix+n)` slots as eagerly-spilled virtual registers, uses the existing
	CFG/effects/liveness, computes conservative reaching definitions (calls,
	opaque instructions and indirect writes kill frame definitions), and reports
	same-block, cross-block, parameter-entry, full-span and split-region
	candidates under `-fstats`. `DCCPEEP_FRAME_REPORT=1` additionally prints exact
	line endpoints for profile correlation. It changes no program text.

	The measured stop condition is decisive. Across the complete corpus, 39,196
	surviving frame loads reduce to 1,025 loads with a unique cross-block store,
	5,030 parameter-entry loads and 4,049 ambiguous loads at joins. Endpoint-only
	availability leaves 2,688 loads, but requiring BC/DE to be free over the
	complete value range leaves just 27 values / 60 uses / 387 static T-states.
	Live-range splitting recovers only 48 regions / 102 uses / 618 static
	T-states. tchess - where frame access is 39.3% of executed app cycles - has
	zero full-span candidates and one two-use DE split region worth 11T
	statically; dynamic profile correlation shows that region is cold.

	Therefore do **not** implement rewriting, written-value spill splitting, or
	retire existing allocators on top of this analysis: the plan explicitly made
	those phases conditional on material candidate supply, and the supply is
	orders of magnitude too small. The 39.3% frame-access headline is real, but
	almost all of it occurs while BC/DE already carry live values, crosses calls,
	or has multiple reaching definitions. Capturing it requires a different
	compiler IR/allocation architecture, not another dccpeep pass.

### MIR prototype

`src/dcc/dcc_mir.c` / `dcc_mir.h` are the analysis-only first slice of that
different architecture. dcc builds one statement AST at a time and resets its
arena immediately after emission, so the prototype lowers each statement into
a persistent per-function stream *before* physical Z80 register assignment.
The stream has unlimited virtual values, loads/stores, constants, unary/binary
operations, indexed loads, calls/arguments, labels, branches, phi-like merges
and returns. `&&` is lowered with real short-circuit control flow - the RHS is
not reachable from the false-LHS edge - rather than an eager binary operation.
Unsupported semantics (currently member expressions, compound assignments,
`||`, conditional expressions, switch/goto and declaration replay) remain
explicit `opaque` barriers rather than being represented incorrectly.

Enable a dump without changing codegen:

```sh
DCC_MIR_FUNCTION=is_attacked ./dccmake tests/tchess.c \
    dcc-output=MI dcc-peep=false
# Or DCC_MIR_REPORT=1 for every function attempt.
```

Each dump names its emit-sink purpose because speculative regalloc can generate
the same function more than once under a VERIFY sink. The verifier resolves
branch labels, builds instruction successors, checks virtual use-before-def and
duplicate definitions, solves iterative backwards virtual-value liveness, and
reports block count, maximum live pressure and opaque-barrier count. The
The first `is_attacked` milestone was 25 MIR blocks, 100 virtual values,
max-live 49, 12 opaque barriers and 0 verifier errors. Adding semantic
AST_INDEX and AST_LOGAND lowering removes every barrier in that function: 49
blocks, 222 virtual values, max-live 26, 0 opaque barriers, 0 verifier errors.
The edge-specific liveness gate is cleared for AST_LOGAND phis: each input records its supplying
predecessor label and is live only on that incoming edge. `is_attacked`'s
max-live pressure drops from the conservative 26 to 3 before object promotion.

The prototype also has conservative scalar mem2reg. Exact `Sym` metadata gives
an object identity only to non-volatile, non-array, 1/2-byte locals and
parameters whose address is never taken. Parameters receive explicit entry
definitions. Forward dataflow folds a load only when every CFG predecessor
agrees on one stored virtual value; ambiguous joins and opaque barriers remain
memory operations rather than getting synthetic object phis. On `is_attacked`,
six objects fold 14 loads and expose four persistent values crossing calls.

`mir_simulate_allocation` builds virtual-value interference from MIR liveness
and greedily colors HL/DE/BC/IY. Values crossing calls may use only callee-saved
IY; opaque-crossing values spill. HL-fixed operation results are boundary
constraints, not lifetime-long precolors: if the allocated home differs, the
simulation counts a register move, which models live-range splitting. For
`is_attacked`: max-live 4 after object promotion, zero spills, four
call-crossing values, and 19 required fixed-result moves. This is analysis,
not emitted Z80; instruction-specific operand constraints still need to be
added before the coloring is authoritative.

The first emitted-Z80 gate is opt-in through
`DCC_MIR_EMIT_FUNCTION=<exact-name>`. `mir_begin_function` redirects the
existing body to a temporary stream after its assembler label while preserving
the original FINAL/VERIFY/DEFERRED sink purpose; at
`mir_end_function`, verified MIR is emitted to a second temporary stream and
committed only if the strict selector accepts it. Otherwise the captured
existing body is copied back byte-for-byte. Partial MIR output can therefore
never contaminate fallback.

`DCC_MIR_CANDIDATES=1` dry-runs all strict selectors and reports accepted
function names without replacing code. `DCC_MIR_EMIT_ALL=1` transactionally
tries every function, but automatically commits only the allocation-backed
countdown and accumulator loop selectors with measured wins. Exact-name mode
retains the straight-line and comparison selectors for development. Automatic
use of those selectors caused 29 perf-baseline regressions because existing
dccpeep already removes more frame traffic from tiny helpers; semantic
acceptance is therefore not a profitability decision. Emit-all is quiet unless
one of the explicit MIR report variables is also set. The automatic gate
accepts only ordinary 16-bit `int` returns, rejects pointer parameters because
MIR does not yet represent pointer-arithmetic scaling, and emits the
standard `extrn __stchk / call __stchk` immediately after establishing IX when
`-fstack-check` is active. Both the default stack-check and no-stack-check fast
correctness suites pass all 309 runnable apps with emit-all enabled, including
diagnostics and dccpeep fixtures. Full peep+nopeep validation also passes all
correctness and checked performance baselines.

The initial selector intentionally supports only one straight-line word return:
a parameter, a constant, parameter +/- constant, or two parameters added or
subtracted. It emits the ordinary IX frame and epilogue and recognizes `+/-1`
as `inc/dec hl`.

Plain scalar declaration initializers are captured explicitly because they
bypass statement AST emission. `mir_set_initializer_target` in `dcc_decl.c`
names the local, and `ast_emit_init_expr` lowers the initializer and records a
MIR store. Conservative mem2reg can then remove the local object entirely.
`int x=a+1; return x+2;` emits as parameter `a + 3`, with no frame slot.

Focused runtime tests `local1(39)`, `sum2(20,22)` and `diff2(50,8)` all emit
MIR and return 42; targeting unsupported `mul2` reports `result=fallback`,
produces a byte-identical original body, and also returns 42.

The first CFG selector accepts exactly
`if (a <comparison> b) return C1; return C2;` for word parameters and constant
returns. `== != < >= > <=` are supported; `>` and `<=` normalize by swapping
operands. Signed order biases both high-byte sign bits before the ordinary
16-bit subtract; unsigned/pointer order uses carry directly. Boundary tests
cover `-1/1` and `65535u/1u` for all four relational directions. Other branch
graphs still fall back transactionally.
Do not widen this subset without a focused runtime comparison and a fallback
identity check.

Scalar compound assignments and prefix/postfix inc/dec lower as explicit
load/binary/store operations. At a labeled two-predecessor join, the first
ambiguous object load may become an object phi when both predecessor states
provide distinct known values; the phi is associated with those predecessor
labels and dataflow is rerun. Unlabeled or multi-predecessor joins remain
memory. This is enough to form induction-variable SSA for simple loops.

The first allocation-backed loop selector accepts exactly
`while (n > 0) --n; return n;` for one word parameter. It materializes `n` in
BC at entry, keeps it there across the complete loop, and copies BC to HL only
for the return. Signed and unsigned termination checks are separate. Against
the existing peep-optimized compiler on 40 calls of `down(30000)`, cycles fall
from 67,225,169 to 60,022,609: **-7,202,560 / -10.71%**, identical output.

Loop headers now receive object-merge placeholders for every promotable object
known before the loop. At a labeled two-predecessor header, each placeholder
can become an edge-specific object phi; mem2reg reruns after insertion. This
allows values first used after the loop condition (such as accumulators) to
stay in SSA rather than remaining ambiguous memory.

The corresponding two-register selector accepts exactly
`sum=0; while(n>0){sum+=n;--n;} return sum;`. BC holds `n`, DE holds `sum`, and
the update uses `ex de,hl / add hl,bc / ex de,hl`; neither value touches the
frame in the loop. On 4000 calls of `accum(100)`, peep cycles fall from
62,476,309 to 29,356,309: **-33,120,000 / -53.0%**, identical 16-bit output.

The first automatic selector exercised by the checked corpus accepts unsigned
constant-division loops of the exact form
`q=0; while(K<=r){r-=K;++q;} return q;`, where `K` is a positive 16-bit
constant. BC holds the remainder and DE the quotient; `HL=BC-K` supplies both
the carry-based unsigned test and the next remainder. On `tcrcfix:bcd_div10`,
the stack-check full suite reports peep cycles **-5.75%**, peep size **-128
bytes / -1.22%**, and nopeep cycles **-7.95%**, with identical output. This is
the model for automatic rollout: a strict semantic shape plus measured
profitability against both existing backend modes.

The first automatic three-register selector accepts the exact repeated-
invariant-add shape `total=0; for(i=0;i<K;++i){total+=factor;total+=factor;}`.
It chooses the most useful transformed value rather than blindly caching a
source object: IY holds callee-saved `2*factor`, BC holds the induction value,
and DE holds the accumulator. Saving IY before IX shifts parameter offsets by
two; the MIR emitter accounts for that, emits `__stchk` after both saves, and
restores IX then IY. A byte-narrowed induction object is accepted only when its
positive constant bound fits 0..255. On `tbcint:scale_by`, the full stack-check
suite reports peep cycles **-6.66%** and nopeep cycles **-14.92%**, with
identical output and all performance baselines passing. This demonstrates the
unified policy directly: BC and DE carry mutable loop state while IY carries
the profitable invariant that would otherwise consume repeated frame loads.

Object mem2reg uses three distinct negative states: `UNREACHED` is lattice
bottom for an instruction/backedge not visited by the fixed-point iteration,
`UNDEFINED` means a reachable path has no safe object value, and `AMBIGUOUS`
means reachable predecessors disagree. Never initialize loop dataflow with
`UNDEFINED`: meeting an entry parameter with an unvisited backedge would then
falsely make an invariant ambiguous. With `UNREACHED` as the identity element,
`scale_by` promotes both factor loads to its entry parameter SSA value; the
reported max-live becomes the truthful four values and allocation simulation
uses IY. Selector discovery must also ignore stores with `object < 0` and must
not inspect phi fields before the relevant phi exists; ASan/UBSan on `tcaslv`
is the focused regression check for this boundary.

Load-bearing validation for any MIR change while it remains analysis-only:

- `DCC_MIR_REPORT=1` over every `tests/*.c` must report zero `errors=N` where
	N is nonzero.
- Run at least one hot supported function under ASan/UBSan. The CFG successor
	arrays are fixed-size (conditional branches have exactly target+fallthrough),
	so malformed construction must report an invalid edge rather than index a
	liveness matrix out of bounds.
- Raw compiler `.MAC` must be byte-identical to a clean pre-change worktree.
	`tstdc` is the expected exception because it embeds `__TIME__`; inspect its
	diff and require that to be the only changed bytes.
- Run full peep+nopeep `runall.ps1` before committing.

Do not enable MIR emission by default yet. The next architectural gate is replacing opaque
barriers for the integer/pointer subset and proving that MIR CFG/liveness
matches current generated control flow. Physical allocation comes only after
that representation is semantically complete for one whole function.

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
