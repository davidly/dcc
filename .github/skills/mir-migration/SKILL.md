---
name: mir-migration
description: "Develop and validate dcc's generated-only MIR backend. Use when improving MIR selectors/cost policy, comparing generated candidates, measuring MIR coverage, recovering performance, removing discard-only AST/regalloc dependencies, or planning multi-day MIR work."
---

# dcc MIR migration

Use this skill for the staged migration of function code generation to
`src/dcc/dcc_mir.c`. Also load the `dcc-project` skill for general compiler
build and test conventions.

The production compiler is generated-MIR-only:

- Every function is lowered to MIR.
- Production Z80 is copied only from a selected MIR candidate.
- Explicit non-emitting AST metadata processing owns declaration/initializer
  replay, scopes/VLAs, inline temps, strings, labels, diagnostics, and debug
  events. No function-body AST assembly pass or discard sink remains.
- Each function has one production metadata/MIR body walk. Legacy no-IX,
  BC/E, loop-BC, and IY retry drivers and their generated-text postprocessors
  are gone.

Do not reintroduce legacy output as an oracle. Compare generated candidates or
current-vs-parent compilers, improve one structural class, and keep strict
require-emit coverage.

## Non-negotiable rules

1. Never widen generated-candidate eligibility without identifying the exact
   affected functions first.
2. Never update performance baselines to hide a selector regression.
3. Peep and nopeep must both be non-regressing for newly emitted functions.
4. A smaller assembly-text stream or instruction count is not proof of faster
   or smaller Z80 code. Run the affected apps.
5. Keep semantic-risk gates separate from cost gates. Correct-but-slow output
   remains a performance regression until fixed.
6. Do not add app/function-name exceptions to production selection. Derive a
   structural predicate or improve the emitter.
7. Preserve unrelated dirty worktree files. Generated census files belong under
   `build/` and are not committed.
8. Use normal-speed `ntvcm`; do not add a speed override.

## Important files

| Surface | Location |
| --- | --- |
| MIR lowering, analysis, selectors, acceptance | `src/dcc/dcc_mir.c` |
| MIR integration API | `src/dcc/dcc_mir.h` |
| AST metadata replay | `src/dcc/dcc_ast_metadata.c`, `src/dcc/dcc_ast_stmt_meta.c` |
| AST support/initializer compatibility | `src/dcc/dcc_ast_gen*.c`, `src/dcc/dcc_decl.c` |
| Symbols, scopes, VLA metadata | `src/dcc/dcc_symbols.c` |
| Runtime ABI | `DCCRTL.MAC` |
| MIR census and snapshot comparison | `scripts/mir-migration-census.py` |
| Correctness/performance runner | `scripts/runall.ps1` |
| Dynamic PC profiler | `scripts/dccprof.ps1`, `scripts/dccprof.py` |
| Checked performance values | `tests/perf_baselines.csv` |

## Current acceptance barriers

Read the current code before relying on this list. The transactional acceptance
section near `mir_end_function()` has historically included:

- assembly-text size or structural cost limits;
- instruction-count limits;
- static-inline substitution calls;
- pointer-to-array declarations;
- CFG backedges;
- large CFG block counts;
- oversized MIR instruction streams;
- targeted performance gates for known unprofitable VLA shapes.

These are migration boundaries, not permanent architecture. Remove each only
when its underlying selector or cost problem is solved.

## Historical note: `text-size` fallback was systemic (2026-07-30)

The fallback-era sections below are retained only to explain selector design
and rejected experiments. They are not current production workflow.

At a 165/2319 (7.12%) coverage checkpoint, a full census showed **every**
`text-size` fallback (2,109 of 2,319 functions) is attempted through the same
selector, `mir_try_emit_spilled_scalar_cfg`, and bucketing the byte/instruction
gap showed 99.1% are more than 64 bytes over the legacy backend and 96.8% have
an instruction-count gap over 4. This population is **uniformly ~2x more
expensive**, not marginally short — do not assume "near-cost" is still the
dominant shape without re-bucketing the gap first.

Forced-accept diffs of two representative functions (`check_s`, identical
across `tests/tesc.c`/`tstr3.c`/`tsyntax.c`, and `and_expr` in
`tests/adaint.c`, a `while` loop) originally appeared to show a double-
materialization bug in `mir_emit_scalar_compare`. **Update (mir-migration-
plan-next10, Items 1-3): that bug is already fixed.** Code and comments
citing Plan-100 Items 1/4/25/27 (`mir_binary_is_fusable_comparison` +
`mir_emit_fused_comparison_branch`) show the compare+branch fusion was
generalized during Plan-100 and confirmed working correctly via direct
forced-accept inspection of `tests/tmirfuse.c`'s `nseq`/`nsne`/`nult`/`nuge`
whole-function-compare shapes. Do not re-investigate this as if it were
unfixed.

What the historical next-10 plan (preserved in git history) actually found in
`mir_try_emit_spilled_scalar_cfg` (now fixed, commit `b2a7aab`): the
selector unconditionally re-emitted a second, dead, unreachable function
epilogue after its main instruction loop even when the last instruction was
already a `MIR_RETURN` that had emitted its own epilogue. Fixing this is a
pure dead-code removal with no effect on which functions clear the
acceptance gate (the fix intentionally compensates the gate's byte
comparison so it cannot newly promote a function purely due to this
saving — see `mir_spilled_scalar_cfg_elided_epilogue_bytes`).

The next evidence-backed candidate, not yet fixed: a single-use function
parameter is frequently copied into a new backend stack slot and reloaded
from there, rather than being read directly from its stable incoming
`ix+N` offset each time (seen in both `nseq`'s locals and
`tmirslot.dead_store_elision`, growing the frame for no reason). This
requires changing the shared `mir_prepare_backend_slots` interval logic
that every selector's slot decisions depend on, so it is higher risk than
a single-selector fix and deserves a dedicated forced-accept A/B campaign
across a representative population sample before any code change.

Before proposing related selector work, re-run the generated-candidate census
rather than assuming the old fallback-era population still applies.

`mir-text-size-plan.md` is the authoritative chronological migration log and
root `plan.md` is the short current-state handoff. Older completed planning
documents are preserved in git history rather than kept beside the active
plans: overlapping item numbers had started producing "verified already
satisfied" no-ops. When resuming multi-session MIR work, re-derive priorities
from a fresh census and direct assembly inspection instead of continuing stale
item numbering.

## Known pattern: selectors reachable only via a diagnostic env var (2026-08-02)

Plan-100's Phase 4 (Items 45-46) and Phase 8 (Items 78-80) each independently
found a `mir_try_emit_*` selector wrapper that was fully implemented, passed
its own structural gate, but was **never called from the true production
default path** in `mir_end_function()` — only from a diagnostic branch gated
by an env var (`DCC_MIR_EMIT_FUNCTION`/`DCC_MIR_CANDIDATES` in Item 46's case,
`DCC_MIR_EMIT_GENERAL`/`DCC_MIR_EMIT_HOME_CFG` in Item 78's). Two prior staged
rollouts had been built, gated, and diagnosably tested, then simply never
wired in. Before assuming a generated population needs a brand-new selector
built from scratch, grep `dcc_mir.c` for `mir_try_emit_` functions and check
whether each one is reachable from `mir_end_function()`'s unconditional
`else` branch — some may already exist and only need wiring plus an A/B proof.

When such a selector is found, first classify it, because the two cases need
different fixes:

- **Distinct emitter, never used elsewhere** (e.g. `general_rollout` →
  `mir_try_emit_homed_scalar_dag`, used nowhere else in the codebase): a real,
  valuable, promotable candidate. Prove it with a full census cross-reference
  against the current production baseline (env-var-forced sweep, byte-compare
  every match) *and* a forced full-app A/B (`DCC_MIR_FORCE_ACCEPT_FUNCTION`
  or the env var itself with `runall -Mode full`) before promoting — do not
  assume smaller-and-different means strictly better everywhere (Item 78
  found 26 of 27 wins but 1 real loss). Promote with a safe swap: try the new
  selector into a separate stream, and only replace the incumbent's output if
  the alternative structurally matches and is strictly smaller, or the
  incumbent failed outright. This is the same fresh-stream-swap pattern Item
  46 established for its loop-family retry.
- **Wrapper whose body calls an emitter already active in production** (e.g.
  `home_cfg_rollout` → `mir_try_emit_homed_scalar_cfg`, the same function
  production tries unconditionally): provably redundant scaffolding with zero
  possible behavioral difference from the always-on path. Confirm with an
  env-var-forced census showing byte-identical output to production, then
  delete the wrapper and its diagnostic call sites outright — no A/B needed,
  since there is nothing it could add.

## Discipline note: consolidate parallel cost-gate formulas as they accumulate

Multiple `mir_try_emit_*` gates independently recompute the same
frame-size/cost estimate (`mir.local_bytes + mir.aggregate_temp_bytes + 2 *
mir_prepare_backend_slots()`, or a narrower subset of it) inline rather than
calling one shared predicate. This is Item 19's "one-predicate discipline"
lesson recurring at the frame-size layer (found again at Item 86): when a
narrower gate's subset structurally guarantees a term is always zero (e.g.
`aggregate_temp_bytes` is always 0 for any candidate whose opcode whitelist
excludes `MIR_CALL`, since only call-argument struct temporaries increment
it), including the full shared formula anyway is free and keeps the two gates
textually identical instead of silently drifting apart. Prefer factoring such
formulas into one `static int` helper as soon as a second call site appears,
and verify the refactor with a byte-identical regression-gated census diff
before relying on it being behavior-preserving.

## Fast migration loop

### 1. Start from a committed checkpoint

```sh
git status --short
git log -3 --oneline
sh src/dcc/build-dcc.sh
```

Only unrelated known files may remain dirty. Each contributor should work on a
separate branch or worktree based on the latest accepted checkpoint.

### 2. Snapshot before editing

For the whole runnable corpus:

```sh
python3 scripts/mir-migration-census.py \
  --output build/mir-before.tsv
python3 scripts/mir-migration-census.py \
  --extra-args=-fstack-check \
  --output build/mir-before-stackcheck.tsv
```

For a local hypothesis:

```sh
python3 scripts/mir-migration-census.py \
  --apps app1,app2 \
  --output build/mir-before.tsv
```

The complete census skips apps marked `ignore` in
`tests/_test_overrides.json`, forwards per-app `dcc_args`, and has a per-compile
timeout. Pass `--include-ignored` only for deliberate research.

The census runs each app's compile concurrently by default (`--jobs`, default
CPU count; each compile is an independent short-lived subprocess so this
scales well) - output is sorted before being written, so `--jobs` never
changes the resulting `.tsv` (verified byte-identical against `--jobs 1`).
Pass `--jobs 1` only when you need strictly sequential per-app progress
output, e.g. while diagnosing a single hanging compile.

### 3. Choose a batch from evidence

Prioritize in this order:

1. **Repeated selector overhead** affecting several functions, such as dead
   backend slots, redundant register preservation, rematerializable call
   arguments, no-op conversions, or missed constant strength reduction.
2. **Generated-candidate gaps** where machine costs are close. Compatibility
   `captured_*` census columns are always `-1` and must not drive decisions.
3. **A semantic class with an existing focused test**, such as pointer arrays,
   VLA sizes, variadics, aggregates, or div/mod pairs.
4. **Hot loop classes** only after dynamic profiling. Loop backedges are not a
   gate to widen speculatively.
5. **Large CFGs and inline substitution** after smaller selector-quality issues
   are addressed; expanding them early multiplies spill/PHI problems.

Prefer one reusable cause over many individual candidates. A good batch usually
adds 1-20 functions and touches one selector concept.

### 4. Form a falsifiable hypothesis

Examples:

- "Unused call results still receive frame slots."
- "A no-op conversion creates a second virtual home."
- "Two MIR div/mod operations with identical operands can use one runtime
  `__sdivmod` call."
- "This generated candidate removes frame traffic without changing ABI state."

Name the focused app and the command that can disprove the hypothesis before
editing.

### 5. Use MIR diagnostics

```sh
DCC_MIR_SELECT_REPORT=1 ./dcc -stack 512 -I . tests/app.c -o /tmp/app.mac
DCC_MIR_REPORT=1 DCC_MIR_FUNCTION=function \
  ./dcc -stack 512 -I . tests/app.c -o /tmp/app.mac
```

Useful environment controls:

| Variable | Purpose |
| --- | --- |
| `DCC_MIR_SELECT_REPORT=1` | Selector, result, reason, byte/instruction/block metrics |
| `DCC_MIR_REPORT=1` | MIR instructions, liveness, allocation summary |
| `DCC_MIR_FUNCTION=name` | Focus MIR dump on a function |
| `DCC_MIR_COVERAGE=1` | Report opaque lowering nodes |
| `DCC_MIR_REQUIRE_COMPLETE=1` | Fail when opaque MIR remains |
| `DCC_MIR_FORCE_ACCEPT_FUNCTION=name` | Diagnostic only: generate one normally rejected candidate |
| `DCC_MIR_SELECT_FUNCTION=name` | Restrict a generated-candidate comparison |
| `DCC_MIR_SELECT_CANDIDATE=name` | Select one named generated cost candidate |
| `DCC_MIR_DEAD_LOCAL_REPORT=1` | Report safely reclaimable deepest local-frame suffixes |

Force controls must never be used as the production fix. They exist to measure
one candidate and derive a structural acceptance or emitter change.

### 6. Make the smallest reusable edit

Prefer this order:

1. Remove dead MIR values or aliases during deferred metadata repair.
2. Improve backend slot/live-range preparation.
3. Rematerialize constants, addresses, or single-use call arguments.
4. Improve homed register preservation using real use information.
5. Add a proven instruction-selection form.
6. Add a structural acceptance exception only after forced A/B profiling.
7. Add a new semantic lowering/emission class.

Do not optimize by source function name.

### 7. Rebuild and run the cheapest discriminator

```sh
sh src/dcc/build-dcc.sh
pwsh ./scripts/runall.ps1 -Apps app -Mode full -RunTimeout 20
```

During a failing iteration, use one app at a time. `-Mode fast` is acceptable for
an initial correctness check, but a candidate is not complete until `-Mode full`
passes because nopeep often reveals regressions hidden by dccpeep.

### 8. Compare snapshots

```sh
python3 scripts/mir-migration-census.py \
  --output build/mir-after.tsv \
  --compare build/mir-before.tsv \
  --fail-on-regression
python3 scripts/mir-migration-census.py \
  --extra-args=-fstack-check \
  --output build/mir-after-stackcheck.tsv \
  --compare build/mir-before-stackcheck.tsv \
  --fail-on-regression
```

The report distinguishes:

- newly MIR-emitted functions;
- functions that disappeared or stopped reporting MIR;
- apps with any census metric changes;
- apps whose runtime output may change.

Run the generated focused command. It includes newly/removed MIR functions and
already-active MIR functions whose generated metrics changed. Selected-hash changes are included. Captured metrics are compatibility-only.

### 9. Profile when static metrics disagree

If fewer instructions still run slower:

```sh
DCC_MIR_FORCE_ACCEPT_FUNCTION=function \
  pwsh ./scripts/runall.ps1 -Apps app -Mode full -RunTimeout 20

DCC_MIR_SELECT_FUNCTION=function \
DCC_MIR_SELECT_CANDIDATE=spilled-rhs-forward \
  pwsh ./scripts/runall.ps1 -Apps app -Mode full -RunTimeout 20
```

For a hot function, use:

```sh
pwsh ./scripts/dccprof.ps1 app -ProgramArgs ...
```

Compare actual call counts and hot instructions. Typical causes of static metric
misreads include:

- a call replacing an established inline body;
- loss of loop BC/IY registerization;
- runtime division instead of a shift/add sequence;
- extra callee-save setup;
- code-placement sensitivity in interpreter heaps;
- flag-producing operations replaced with flag-neutral instructions.

### 10. Validation tiers

Use the narrowest tier that covers the blast radius.

**Iteration tier**

- host compiler build;
- one focused app, usually fast mode;
- inspect selection report.

**Batch tier**

- every newly MIR-emitted app in `-Mode full`;
- every app with changed already-active MIR output;
- relevant focused dccpeep fixtures if optimizer code changed;
- `git diff --check` and source diagnostics.

**Milestone tier**

Run only after a material coverage jump, semantic gate removal, shared ABI/runtime
change, or before merging:

```sh
pwsh ./scripts/runall.ps1 -Mode full -RunTimeout 20
```

Required result:

- all runnable apps pass;
- diagnostics pass;
- dccpeep fixtures pass;
- performance passes in both modes.

Do not run the full suite after every local edit.

`scripts/runall.ps1` also accepts `-FailFast`: it stops dispatching new apps as
soon as the first correctness failure or per-app performance regression is
seen (apps already in flight finish; not-yet-started apps are reported as
skipped, not failed). Apps already running when the trigger fires still
finish, so it shortens iteration feedback without changing what a full,
unthrottled run reports. Use it while iterating on a batch; still run the full
suite (without `-FailFast`) for the milestone-tier validation before a commit,
so every app's status is actually known rather than short-circuited.

## Prioritization scoring

When several candidates compete, score each class using:

- **Yield:** number of functions/apps unlocked.
- **Reuse:** whether the fix removes a repeated emitter/slot pattern.
- **Risk:** straight-line scalar < acyclic CFG < calls/PHIs < loops/VLAs < large
  CFG/inline substitution.
- **Performance confidence:** forced full-mode A/B result.
- **Test quality:** dedicated regression app and baseline availability.
- **Merge conflict cost:** amount of overlap in `dcc_mir.c` with another owner.

A lower-yield repeated fix is preferable to a broad gate relaxation with no cost
model.

## Multi-person coordination

### Divide work by generated-code class

Assign one owner per class, for example:

- spilled selector slot/rematerialization quality;
- homed CFG moves and PHIs;
- loops/backedges;
- pointer arrays and runtime strides;
- static-inline substitution;
- large CFG compile-time scaling;
- migration tooling and reports.

Avoid two contributors editing the same selector block simultaneously. Use
separate worktrees and rebase onto the latest accepted checkpoint before final
validation.

### Record ownership before editing

In the issue/PR/session handoff, record:

```text
Owner:
Base commit:
Fallback class:
Candidate apps/functions:
Before snapshot:
Hypothesis:
Focused validation command:
Files expected to change:
```

### Keep commits independently valid

Each migration commit should:

- contain one reusable MIR concept;
- keep mixed-mode emission active;
- include no generated census files;
- pass its exact affected-app full-mode set;
- state coverage before/after in the commit or handoff;
- have no unexplained performance baseline updates.

Use commit subjects such as:

```text
dcc MIR: rematerialize constant call arguments
dcc MIR: fold representation-identical conversions
dcc MIR: fuse paired scalar divmod operations
```

### Handoff template

```text
Commit(s):
Coverage before/after:
New MIR functions:
Remaining generated-MIR debts:
Focused tests run:
Full milestone run (if any):
Performance changes:
Rejected experiments and why:
Next recommended class:
Unrelated dirty files left untouched:
```

Document negative results. They prevent the next contributor from repeating a
correct-but-slower gate experiment.

## Baseline policy

`tests/perf_baselines.csv` is a guardrail, not a migration target.

- Never use `-UpdatePerfBaseline` during selector iteration.
- Fix material regressions before accepting any baseline movement.
- Update baselines only after a complete full-mode run proves the new profile is
  intentional and correctness-clean.
- Keep known nondeterministic apps excluded through `perf_ignore` rather than
  accepting noise.

## Completion criteria

The staged migration is complete when:

1. the runnable and extended corpora report generated MIR for every function;
2. a MIR-required build mode passes correctness and diagnostics;
3. peep and nopeep performance are accepted;
4. large CFG compile time is bounded;
5. legacy emission is no longer needed during normal function compilation;
6. obsolete legacy register-allocation retry drivers are removed;
7. declaration, inline, string, label, debug, and deferred-body side effects
   have explicit non-emitting owners.
