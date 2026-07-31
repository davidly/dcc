# MIR migration: path to 100% coverage

## Executive summary

Current state: **185/2378 (7.78%)** MIR coverage (commit `b4d0676`). A fresh
census re-bucketing confirms the systemic finding from the last three
sessions still holds: **2090/2134 (98%) of `text-size` fallback functions
have a >64-byte generated-vs-captured gap** - a uniform ~2x cost penalty,
not a near-miss population. Small, independent instruction-selection folds
(the strategy used for all coverage gained so far: 0% -> 7.78%) are
running out of runway - each session's "next" independent fold candidate
list is shrinking and increasingly marginal (see `mir-migration-plan-100.md`
and `mir-migration-plan-next10.md`'s Execution Logs).

**The root cause, confirmed this session by reading the emitter code
directly**: `mir_try_emit_spilled_scalar_cfg` - the selector that handles
essentially the entire general-CFG population (2253/2378 functions
attempt it) - **never reads `mir.allocation_colors` or
`mir.allocation_spills` at all**. Every single value, in every function
that goes through this selector, is always stored to and reloaded from an
`ix`-relative memory slot, regardless of what the register allocator
decided. This is the entire source of the "uniform ~2x" gap: legacy keeps
hot values in registers across blocks; this selector keeps *nothing* in a
register across an instruction boundary.

The good news: the hard part of a real register allocator - live-range
interference analysis, cross-call/cross-opaque conflict tracking, and
graph-coloring - **already exists and already runs, for every function,
unconditionally** (`mir_summarize_allocation`, `~line 4490` onward). It
just isn't consumed by the general emitter. The only production selector
that *does* consume it, `mir_try_emit_homed_scalar_cfg`, proves the
approach works, but is walled off to functions with **zero spills and no
calls at all** (96/2378 functions currently qualify).

This means the path to materially higher coverage is not "find 100 more
independent small folds" - it is: **teach the general CFG selector to
consult the allocator's output**, closing the gap between "prove it works
on the trivial 4% case" and "use it for the other 94%". This is a large,
multi-session structural project, but it is the single lever big enough to
plausibly move coverage from single digits toward a majority, rather than
another few percentage points.

## Why "100%" is a multi-phase, multi-month goal, stated honestly

Do not read this document as promising 100% coverage in any bounded
number of sessions. Completing the staged migration (per SKILL.md's
"Completion criteria") requires the runnable *and* extended corpora to have
**no unexplained fallback** - every remaining acceptance gate
(instruction-count, cfg-block-count, cfg-backedge, pointer-array,
inline-substitution, oversized MIR streams, large CFG compile-time
scaling) must eventually be closed or proven unreachable, not just the
text-size gate. What follows is a realistic phase order, not a schedule.

## Phase 1 (next, largest expected yield): register-aware general CFG emission

**Hypothesis**: if `mir_try_emit_spilled_scalar_cfg` loads/stores a value
via `mir.allocation_colors[value]` when it is non-spilled (keeping it in
its assigned register across the instruction, exactly as
`mir_try_emit_homed_scalar_cfg` already does), and only falls back to an
`ix`-relative slot when `mir.allocation_spills[value] >= 0`, the generated-
byte and generated-instruction gap against legacy collapses for a large
fraction of the current 2134-function fallback population, without
touching the CFG shape, branch logic, or PHI handling this and prior
sessions have already been improving.

**Why this is scoped as "the next move" rather than attempted in one shot**:
this is not a single patch. It requires, in rough dependency order:

1. **Audit `mir_try_emit_homed_scalar_cfg`'s emission helpers**
   (`mir_emit_virtual_load`/`_store` already have an `allocation_colors`-
   aware branch reachable from the homed path per earlier grep evidence -
   confirm exactly which helper functions already do this, and which ones
   `spilled-scalar-cfg` calls that bypass it entirely).
2. **Cross-block register liveness at merge points**: `homed-scalar-cfg`
   requires zero spills partly *because* it doesn't need to reconcile
   "value X is in register R at the end of block A, but a different
   register at the start of block B" - PHI/merge-point register identity
   must be handled explicitly once real registers survive across blocks.
   This is the crux of the added complexity and the primary correctness
   risk (get it wrong and PHI copies silently read the wrong register).
3. **Call-clobber correctness**: any value kept in a register across a
   `MIR_CALL` must be proven not to be a caller-saved register the call
   destroys, or must be spilled/reloaded around the call specifically -
   `mir.allocation_colors`'s cross-call tracking should already encode
   this (the interference builder computes `cross_call` per value; verify
   it forces spills or fixed-safe colors for any value live across a call
   before trusting it in this new emitter).
4. **Incremental rollout, not a flag flip**: implement as a new selector
   (or a mode flag inside the existing one) tried *before* the current
   all-spill path, so any CFG shape it can't yet handle correctly falls
   through to the existing (slower but proven-correct) spilled path
   unchanged - preserving SKILL.md's "improve one class, leave everything
   else on fallback" discipline. Start with the narrowest safe subset
   (e.g., straight-line/acyclic CFGs, no calls) and widen only after each
   subset is fully `-Mode full -Extended` clean, exactly like
   `homed-scalar-cfg`'s existing gate list but relaxed one condition at a
   time (first: allow calls with correct spill-around-call handling; then:
   allow `allocation_spill_count > 0` by falling back to slots only for
   the specific spilled values; then: allow the wider instruction/type
   set `spilled-scalar-cfg` already supports).

**Suggested first concrete slice** (small enough to validate in one
session): extend `mir_try_emit_homed_scalar_cfg` itself to permit
`MIR_CALL` when every live-across-call value is provably spilled or
fixed-color-safe per the existing interference data, instead of building
a whole new selector immediately. This directly grows the *already-
correct* 96-function population with the least new emission-logic risk,
and produces real evidence (yield, blast radius, regression rate) before
committing to the harder general-spill-aware emitter in item 1-4 above.

**Falsifiable check before starting**: pick 5-10 representative
fallback functions from the `text-size` bucket that contain exactly one
call and otherwise fit `homed-scalar-cfg`'s existing instruction
whitelist, forced-accept them today (expect rejection today, since calls
are unconditionally disallowed), and hand-verify what legacy actually does
around the call site for the same values - confirms the hypothesis before
touching the allocator-consuming code.

## Phase 2: close the fused-comparison-branch latent bugs found this session

`mir_emit_fused_comparison_branch`'s Item-27 (signed-zero-sign-test) path
has a real correctness bug, reproduced at the `88d28d1` baseline via
`DCC_MIR_FORCE_ACCEPT_FUNCTION=assign_pre` against `forint`, independent
of any acceptance-gate threshold. `cobint`, `tinline`, and `tinlinfb` also
newly failed when an unrelated gate was experimentally relaxed this
session (see `mir-migration-plan-next200.md`'s Item 1 entry) - these are
very likely the same or a closely related bug class, currently masked by
conservative acceptance gates rather than fixed. **Automatic bisection
(using the new `scripts/mir-migration-bisect.sh`, see "Automating the
migration loop" below) sharpened this further**: `forint.bump_sym_val` is
independently buggy too, not merely collateral from `assign_pre` - forcing
either one alone to fallback still fails `forint`, but forcing both (via
`DCC_MIR_FORCE_FALLBACK=1`) fixes it, so there are at least two distinct
latent bugs in this one app alone. This needs its own focused
investigation (start with `forint.assign_pre` and `forint.bump_sym_val`
separately, `DCC_MIR_REPORT=1` dump each, compare fused-branch output
against legacy bit-for-bit on several concrete input values) before any
further widening of the gates that currently keep these functions safely
on fallback. Fixing real bugs behind existing gates is lower-risk and
higher-value than widening gates further.

## Phase 3: frameless leaf-function selector path

Documented as a deferred finding in `mir-migration-plan-next10.md`'s
Execution Log: functions legacy replays with a frameless SP-relative
convention (no `push ix`/`ld ix,0`/`add ix,sp`) are unconditionally forced
into a full `ix`-frame by every current selector, which can make "smaller
MIR instruction text" a false signal of "actually cheaper code" (the classic
skill-rule-4 hazard). Scoping this needs a forced-accept prevalence survey
(how many of the 44 near-miss + how many of the 2090 far-miss functions
would this affect) before choosing an implementation shape.

## Phase 4: remaining acceptance gates (cfg-block-count, cfg-backedge,
pointer-array, oversized MIR streams, inline-substitution)

Lower priority until Phase 1 materially changes the `text-size` gap
landscape - most of these gates currently affect small function counts
(3, 2, 1, and 25 respectively at this checkpoint) and are exactly the
"large CFG / inline substitution" class SKILL.md flags as highest-risk
and lowest-priority until smaller selector-quality issues are addressed.

---

## Automating the migration loop

Several concrete process changes can materially speed up each iteration
without changing the engineering approach above:

### 1. `scripts/mir-migration-validate.sh` - fail-fast validation harness

`runall.ps1` already supports both `-FailFast` (abort remaining apps once
one fails, checked in this session: `scripts/runall.ps1:62,260,1743-1886`)
and parallel per-app execution via `-ThrottleLimit` (default
`[Environment]::ProcessorCount`, confirmed already wired through
`ForEach-Object -ThrottleLimit $ThrottleLimit -Parallel` at
`scripts/runall.ps1:1756,2021`) - so neither fail-fast nor parallelism
needs to be built from scratch; they need to be *used consistently* and
*chained* across the separate build/census/focused/wide/extended tiers,
which today is done by hand, one tool call at a time, re-reading each
stage's tail output before deciding whether to proceed. A single entry
point implementing the mandatory validation ladder end-to-end, stopping
at the first failing tier, removes that manual re-checking step:

```sh
#!/bin/sh
set -e
sh src/dcc/build-dcc.sh
python3 scripts/mir-migration-census.py \
    --output "build/mir-$1.tsv" --compare "$2" --fail-on-regression
pwsh ./scripts/runall.ps1 -Apps "$3" -Mode full -FailFast -RunTimeout 20
pwsh ./scripts/runall.ps1 -Mode fast -FailFast -RunTimeout 20
pwsh ./scripts/runall.ps1 -Mode full -Extended -FailFast -RunTimeout 20
```

`set -e` plus each stage's own `-FailFast` means the very first failure
anywhere in the whole ladder - not just within one `runall.ps1` invocation -
stops the run immediately with the failing stage's output already printed,
instead of a human noticing a failure buried in tail output several tool
calls later.

### 3. Automatic regression bisection

When a wide validation run fails after a gate change (as happened this
session with the inline-substitution relaxation), the manual process was:
census delta -> guess candidate functions -> forced-accept/forced-fallback
each one by hand. This has been scripted as
`scripts/mir-migration-bisect.sh <app> <candidate1,candidate2,...>`: it
loops `DCC_MIR_FORCE_FALLBACK_FUNCTION=<each candidate>` + a quick
`-Mode fast` run per candidate, reporting which function(s), when forced
back to legacy fallback alone, make the app pass again.

**Verified working against this session's own regression**: re-applying
the reverted inline-substitution relaxation and running
`scripts/mir-migration-bisect.sh forint assign_pre,bump_sym_val`
correctly reported that *neither* function's fallback alone fixes
`forint` - which, cross-checked with `DCC_MIR_FORCE_FALLBACK=1` (fixes
it), revealed a sharper finding than this session's manual investigation
originally had: **both `forint.assign_pre` and `forint.bump_sym_val` are
independently buggy**, not just `assign_pre` as manually confirmed at the
time. This mechanical loop found that in under two minutes, versus the
multi-step manual `DCC_MIR_REPORT`/assembly-diff investigation the
original bisection needed - exactly the speedup this tool is meant to
provide. Both functions remain a documented candidate for the Phase 2
fused-comparison-branch investigation above (not yet root-caused which
bug each hits).

### 4. Extend `mir-migration-census.py` with automatic candidate ranking

The census already buckets by fallback reason; it does not yet rank
*within* the `text-size` bucket by a reuse/yield heuristic (e.g., group
by shared structural shape - same selector, similar block count, similar
gap-per-instruction - so a repeated pattern across many functions surfaces
automatically instead of requiring a human to eyeball the tsv and notice
"the `tmirfuse` compare family" or "the shared `check()` utility" by hand,
as happened manually in both this and the prior session). A `--rank-by
reuse` mode that clusters functions by (selector, fallback reason,
byte-gap bucket, first-diverging-instruction-class) would turn "read the
whole tsv by hand" into "read the top 10 clusters."

These four are ordered by effort-to-value ratio: (1) and (2) are small,
immediately useful scripts; (3) meaningfully speeds up exactly the kind of
debugging this and the prior session spent the most wall-clock time on;
(4) is the most valuable long-term but requires the most design work
(defining a good clustering heuristic).

## Execution Log

(To be appended as Phase 1's incremental slices land, in the same style as
`mir-migration-plan-100.md` and `mir-migration-plan-next200.md`.)
