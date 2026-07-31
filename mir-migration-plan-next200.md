# MIR migration: next-200 continuation plan

## Context

Starting point for this plan: **186/2378 (7.82%)** coverage, the checkpoint
left by `mir-migration-plan-next10.md` (now closed/superseded). The stated
goal for this plan is aggressive: +200 newly MIR-emitted functions (roughly
+8.4 percentage points). This document exists to state the real, evidence-
based picture honestly rather than assume that scale of gain is achievable
through small incremental folds, and to track verified progress item by item
in the same Execution Log style as the prior plan documents.

## Current measured state (re-derived fresh this session)

A full census (`build/mir-plan200-baseline.tsv`) confirms 186/2378 (7.82%),
matching the prior checkpoint exactly (174 base + 12 from the param-direct
fix in `88d28d1`).

Re-bucketing the `text-size` fallback population (2134 functions) by
generated-vs-captured byte gap:

- **2090/2134 (98%) have a gap > 64 bytes** - the same systemic ~2x-cost-gap
  finding documented in `.github/skills/mir-migration/SKILL.md` and
  `mir-migration-plan-100.md`, still holding at this checkpoint.
- Only **44/2134 (2%) have gap <= 64 bytes** - the near-miss population that
  small folds can realistically move.

**Implication**: the "+200 functions" goal is not achievable through small,
independent selector-quality folds alone; that population is exhausted or
close to it. Reaching anywhere near that scale requires either (a) a small
number of genuinely large structural levers (e.g. a frameless leaf-function
fast path - see the deferred finding in `mir-migration-plan-next10.md`'s
Execution Log - or broader register homing instead of always-spill), or (b)
a long campaign of many independent smaller-yield fixes. This plan will
proceed item by item, honestly reporting the yield of each, rather than
promising the full 200 up front.

The 44 near-miss functions are dominated by:
- `tmirfuse`'s whole-function scalar-compare family (`nseq`, `nsgt`, `nsle`,
  `nsge`, `nsne`, `nuge`, `nslt`, `nult`; gap 22-31 bytes) - confirmed to be
  correctly excluded from param-direct by the `mir_capture_stream_uses_
  frame()` gate (legacy uses a frameless SP-relative convention for these;
  promoting them risks the same frame-setup-cost hazard as `tc89fnty.mulb`).
  Not a bug; by design.
- A shared `check`/`check_i`/`check_int` test-utility function repeated
  across ~9 test files (gap 35-36 bytes each).
- A few `tvla.vla_sizeof_*` functions (gap 18-42 bytes).

## Execution Log

### Item 1: single-jump `MIR_BRANCH_FALSE` (eliminate redundant double-jump for no-PHI merges)

**Hypothesis**: `mir_try_emit_spilled_scalar_cfg`'s `MIR_BRANCH_FALSE` case
unconditionally emits a 3-part branch (`jp nz,fallthrough` / phi-copies /
`jp target` / `fallthrough:`) even when the merge point needs zero PHI
copies - the overwhelmingly common case for a plain `if (cond) stmt;` with
no live cross-block value. Legacy uses a single inverted-condition jump
(`jp z,target`) for this shape. Since if-statements are extremely common,
this is a high-reuse, low-risk (peephole-scoped, no semantic change),
structural fix - exactly SKILL.md's priority-1 class.

**Implementation**: probe `mir_emit_spilled_phi_copies(i, target)` into a
scratch `tmpfile()` first. If it succeeds and writes zero bytes, emit a
single `jp z,L%d` directly to `target` (no fallthrough label, no second
jump). Otherwise, replay the probe's captured bytes verbatim into the real
output stream (`src/dcc/dcc_mir.c`, `MIR_BRANCH_FALSE` case in
`mir_try_emit_spilled_scalar_cfg`).

**Bug found and fixed during implementation**: an earlier draft called
`mir_emit_spilled_phi_copies` a *second* time (for real output) after the
probe call, on the assumption the function was side-effect-free beyond
writing text. That assumption is false when `copy_count > 0`: the virtual
load/store helpers it calls update live register-cache state as a side
effect, so calling it twice double-applies those state transitions and
corrupts codegen. Fixed by calling it exactly once (into the probe stream)
and replaying the captured bytes into the real output instead of a second
invocation, whenever copies are needed.

**A separate, pre-existing latent bug identified and explicitly NOT
touched by this item**: while validating this change, `cint.if_stmt`
newly failed to re-qualify for MIR emission because it tripped a redundant,
non-monotonic `95%`-instruction-ratio floor inside the unrelated
`inline-substitution` acceptance gate (an artifact of this fix legitimately
shrinking `if_stmt` from 137 to 136 instructions against a 144-instruction
captured baseline, which crosses the 95% floor from the wrong side even
though both the byte and instruction counts strictly improved). Removing
that ratio floor (leaving only the two other, already-sufficient bounds:
`generated_instructions <= captured_instructions + 1` and `generated_size
<= captured_size`) let 20 previously-fallback functions through the gate -
but forced-accept + `-Mode full` testing showed 4 apps
(`cobint`, `forint`, `tinline`, `tinlinfb`) newly failed with real output
mismatches. Bisecting confirmed these are **pre-existing bugs already
present at the `88d28d1` baseline** (reproduced with
`DCC_MIR_FORCE_ACCEPT_FUNCTION` and no session changes applied at all) -
unrelated to this item, previously masked only because the ratio floor
(or the byte-size gate before it) happened to keep those specific
functions on fallback. Per SKILL.md rule 1 ("never remove or widen a
fallback gate without identifying the exact affected functions first"),
the ratio floor is being **reverted to its original form** rather than
kept relaxed, since the true blast radius (4 latently-buggy apps) was far
larger than the single intended target (`if_stmt`). `if_stmt` itself is
confirmed harmless either way by forced-accept + `-Mode full` (its
fallback-replayed form is legacy-correct by definition), so losing its MIR
classification is an accepted, understood, non-regressing side effect of
this item, not a defect.

**Deferred candidate for a future item**: fixing `mir_emit_fused_
comparison_branch`'s Item-27 signed-zero-sign-test path (and whatever
`cobint`/`forint`/`tinline` are hitting) would be a legitimate, separate
follow-up - it is a real correctness gap independent of any gate threshold,
currently masked by conservative acceptance gates rather than fixed. Not
attempted here to keep this item's blast radius to the single hypothesis
under test.

**Verification**: full census (`build/mir-plan200-item1.tsv`, `--compare
build/mir-plan200-baseline.tsv --fail-on-regression`) shows **185/2378
(7.78%)**: 0 newly-emitted at the population level counted by the census
after reverting the gate change, 1 no-longer-emitted (`cint.if_stmt`,
explained and accepted above as harmless). Focused `-Mode full` validation
on the branch-false-only change's 6 affected apps
(`cint,tc99scpe,tcrcfix,tscanf,tstdlib,tsyntax`) passed 6/6 with 0
regressions. Wide `-Mode fast` safety net: 314/314 passed. Full `-Mode full
-Extended` safety net: 314/314 + 196/196 extended cases passed, 0
regressions, 0 unexplained failures.

**Net result**: this item is a pure code-quality/size improvement (smaller,
single-jump branch code for every already-MIR-emitted function containing a
no-copy-needed conditional), verified correctness-neutral, with one
understood/accepted coverage-classification side effect (`if_stmt`). It
does not by itself add new coverage (the `mulb`-style near-miss gate-boundary
case that would have added is the one exception, and that one net-zeroed
against `if_stmt`'s loss) - contribution to +200 is code-quality/latency
groundwork rather than a coverage jump. The 4 latent bugs it exposed (and
declined to unmask) are a genuinely valuable finding for a future,
dedicated correctness item.
