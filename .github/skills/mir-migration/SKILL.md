---
name: mir-migration
description: "Stage and coordinate dcc's migration from legacy AST Z80 emission to active MIR emission. Use when expanding MIR rollout, investigating MIR fallback reasons, improving dcc_mir.c selectors, force-profiling one candidate, measuring MIR coverage, validating affected apps, planning multi-day or multi-person MIR work, or removing a transactional fallback gate."
---

# dcc MIR migration

Use this skill for the staged migration of function code generation to
`src/dcc/dcc_mir.c`. Also load the `dcc-project` skill for general compiler
build and test conventions.

The production compiler is deliberately mixed-mode:

- Every function is lowered to MIR.
- Accepted functions emit Z80 through a MIR selector immediately.
- Rejected functions replay the captured legacy backend output.
- Migration is complete only when MIR-required mode covers the corpus and the
  legacy emitter can be removed.

Do not disable current MIR emission while working on later stages. Improve one
class, admit it transactionally, and leave all other functions on fallback.

## Non-negotiable rules

1. Never remove or widen a fallback gate without identifying the exact affected
   functions first.
2. Never update performance baselines to hide a selector regression.
3. Peep and nopeep must both be non-regressing for newly emitted functions.
4. A smaller assembly-text stream or instruction count is not proof of faster
   or smaller Z80 code. Run the affected apps.
5. Keep semantic-risk gates separate from cost gates. Correct-but-slow output is
   still fallback output until fixed.
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
| Function capture/replay | `src/dcc/dcc_func.c` |
| AST expression emission and static inlining | `src/dcc/dcc_ast_gen_expr.c` |
| AST statement emission | `src/dcc/dcc_ast_gen_stmt.c` |
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

## Known root cause: `text-size` fallback is systemic, not near-miss (2026-07-30)

At a 165/2319 (7.12%) coverage checkpoint, a full census showed **every**
`text-size` fallback (2,109 of 2,319 functions) is attempted through the same
selector, `mir_try_emit_spilled_scalar_cfg`, and bucketing the byte/instruction
gap showed 99.1% are more than 64 bytes over the legacy backend and 96.8% have
an instruction-count gap over 4. This population is **uniformly ~2x more
expensive**, not marginally short — do not assume "near-cost" is still the
dominant shape without re-bucketing the gap first.

Forced-accept diffs of two representative functions (`check_s`, identical
across `tests/tesc.c`/`tstr3.c`/`tsyntax.c`, and `and_expr` in
`tests/adaint.c`, a `while` loop) both showed the same bug: `mir_emit_scalar_compare`
(`dcc_mir.c` ~line 4939) unconditionally materializes an explicit 0/1 boolean
(a label + `inc l` dance) for *every* comparison, which then gets spilled to a
backend slot and reloaded later just to be re-tested by `MIR_BRANCH_FALSE`
(whose own test sequence, `ld a,h / or l / jp nz`, is already fine in
isolation). The narrow `mir_try_emit_comparison_branch` selector already fuses
compare+branch directly, but only for the whole-function shape `if (param OP
param) return A; return B;` — it never fires for the general case, which is
what `mir_try_emit_spilled_scalar_cfg` handles for the other 91% of the
corpus. `dccpeep`'s same-basic-block redundant-reload pass removes *one* of
the resulting store/reload round-trips but not the second, which is a
genuinely dead store (never reloaded anywhere in the function) — a different
bug class the existing peephole passes don't target.

Before proposing new fallback-reduction work, re-run the census and re-bucket
the gap (see `mir-migration-plan-100.md`'s "current measured state" section
for the exact commands) rather than assuming the old "near-cost real
functions" priority still applies — at this checkpoint it barely did (3
functions total).

A fresh, evidence-grounded 100-item continuation plan reflecting this finding
lives at `mir-migration-plan-100.md` (repo root). It supersedes any earlier
plan document of the same or similar name: two prior documents reused
overlapping item numbers across separate files and had started producing
"verified already satisfied" no-ops, a sign that vein of small, independent
pattern folds (constant materialization, alias folding, divmod fusion, etc.)
was running dry. When resuming multi-session MIR work, re-derive the plan from
a fresh census and direct assembly inspection rather than continuing stale
item numbering, and retire the old plan document once its content is folded
into the new one.

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

### 3. Choose a batch from evidence

Prioritize in this order:

1. **Repeated selector overhead** affecting several functions, such as dead
   backend slots, redundant register preservation, rematerializable call
   arguments, no-op conversions, or missed constant strength reduction.
2. **Near-cost real functions** where generated and captured metrics are close.
   Exclude standalone static-inline bodies that legacy intentionally omits.
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
- "This fallback is only a text-size artifact; machine behavior is equal."

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
| `DCC_MIR_FORCE_FALLBACK_FUNCTION=name` | A/B one active MIR function against legacy output |
| `DCC_MIR_FORCE_FALLBACK=1` | Diagnostic only: replay all legacy output |

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
```

The report distinguishes:

- newly MIR-emitted functions;
- functions that unexpectedly returned to fallback;
- apps with any census metric changes;
- apps whose runtime output may change.

Run the generated focused command. It includes newly/removed MIR functions and
already-active MIR functions whose generated metrics changed; fallback-only
metric churn is excluded.

### 9. Profile when static metrics disagree

If fewer instructions still run slower:

```sh
DCC_MIR_FORCE_ACCEPT_FUNCTION=function \
  pwsh ./scripts/runall.ps1 -Apps app -Mode full -RunTimeout 20

DCC_MIR_FORCE_FALLBACK_FUNCTION=function \
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

### Divide work by fallback class

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
Remaining fallback reasons:
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

1. the runnable and extended corpora have no unexplained fallback;
2. a MIR-required build mode passes correctness and diagnostics;
3. peep and nopeep performance are accepted;
4. large CFG compile time is bounded;
5. legacy emission is no longer needed during normal function compilation;
6. capture/replay and obsolete legacy register-allocation paths can be removed
   in separate cleanup commits.
