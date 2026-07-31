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
session) - **superseded, see Execution Log Items 1, 3, and 7**: this
originally proposed extending `mir_try_emit_homed_scalar_cfg` to permit
`MIR_CALL` when every live-across-call value is provably spilled or
fixed-color-safe. Items 1 and 3 (below) landed exactly this - calls to
both in-TU-defined and external/library callees are now accepted,
excluding only indirect calls - leaving the zero-spill requirement
itself as the true remaining lever. Item 7 measured that lever's yield
directly (a corpus-wide survey: only 11 functions, 8 of them single-
spill) and judged it too small to justify the regression risk of adding
spill-aware branches to `homed-scalar-cfg`'s shared emission helpers,
which the already-correct 96+-function zero-spill population also
depends on. The falsifiable check below is likewise superseded by
Item 7's direct measurement.

**Falsifiable check before starting** (superseded by Item 7's direct
survey): pick 5-10 representative
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
on fallback in production - **no shipped behavior is at risk today**;
`assign_pre`/`bump_sym_val` remain on the fallback path by construction of
the existing acceptance gates, and this whole section is pre-emptive
investigation for a *future* gate-widening, not a live regression.

**Progress this session (2026-07-31): reproduced and narrowed, root cause
still open.** Re-confirmed both functions are still independently
implicated via `scripts/mir-migration-bisect.sh forint
assign_pre,bump_sym_val` (both PASS individually when forced to fallback,
matching the prior session's finding). Added new evidence for
`assign_pre`'s specific failure signature: comparing raw `ntvcm` output
byte-for-byte between the unforced build and
`DCC_MIR_FORCE_ACCEPT_FUNCTION=assign_pre` shows the forced build produces
**zero bytes of output** where the correct build produces 192 bytes (a
full digit-computation result) - i.e. this is not a subtle off-by-one in
one computed value, it is a total loss of all subsequent program behavior,
consistent with a stack- or control-flow-level corruption rather than an
arithmetic error. Manually traced the generated assembly for `assign_pre`'s
`idx=idxe>=0?eval_e(idxe):0` ternary (the Item-27 signed-zero-sign-test
fusion) instruction-by-instruction for both `idxe>=0` and `idxe<0` concrete
cases and found the `bit 7,h` / `jp z,...` branch logic, and the two
paths' PHI-merge stack balance, to be individually correct by hand
simulation - the bug (if it is Item 27's fusion specifically, rather than
something else in `mir_try_emit_spilled_scalar_cfg`'s general call/frame
handling that this function also exercises) was not isolated to a single
instruction this session. An attempt to instrument `assign_pre` with an
inserted `fprintf` trace call to compare live variable values at runtime
between builds was inconclusive: it changes the function's MIR shape
enough (adding a 5-argument variadic call) that it may be exercising a
different code path than the original bug, and is not trustworthy evidence
either way - noted here so a future session doesn't repeat it without this
caveat.

**Deferred, with rationale, same as Item 6/the shift-slice above**: fully
isolating this bug requires instruction-level register tracing across a
call boundary (e.g. a custom `ntvcm` single-step trace, or bisecting the
generated assembly by selectively reverting individual emission choices
inside `mir_try_emit_spilled_scalar_cfg` for just this one function) - a
multi-hour, dedicated-session task, not a continuation of the current
session's budget. Recommended concrete next steps for whoever picks this
up: (a) get a Z80 instruction-level trace of `assign_pre`'s first call
site's return address and stack pointer immediately before/after the call,
to confirm or rule out a stack-imbalance/corrupt-return hypothesis; (b)
bisect within `assign_pre` itself by locally reverting just the Item-27
fusion (temporarily forcing `mir_fused_compare_is_signed_zero_sign_test`
to always return 0 for this one function only, as a diagnostic, not a
production change) to see whether the failure disappears - this would
conclusively confirm or rule out Item 27 as the culprit, separating it from
whatever else `spilled-scalar-cfg` does for this function's calls/frame.
No source change made this session as a result; the acceptance gates
already keep this class safely on fallback in production, so there is no
regression to fix, only future-coverage work to unblock.
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

**Prevalence survey run (2026-08-01), Phase 3 retired - zero yield
available:** scanned every currently-runnable app, classifying each
function by its `DCC_MIR_SELECT_REPORT` outcome and scanning its emitted
`.mac` body for the presence/absence of `push ix`. Result: **0 of 1320
functions currently on `result=fallback reason=text-size`** are captured
with legacy's frameless convention - every one of them is captured
`ix`-framed already. (A broader whole-corpus scan found 118/1771 frameless
bodies overall, but these are all either already-MIR-accepted functions -
`homed-scalar-cfg` itself already emits genuinely frameless, SP-relative
code today, e.g. `tbug2.gt_post`, directly contradicting this phase's
original premise that "every current selector" forces an `ix` frame - or
fallback functions for a reason other than `text-size`.) Legacy's own
`function_qualifies_for_speculative_noix` pre-filter (`dcc_regalloc.c`
~line 58) evidently never selects the frameless convention for any
function currently blocked on `text-size`, so there is no fallback
population left for a frameless MIR selector path to unlock right now.
**Retired as a lever for the current checkpoint** - not implemented; revisit
only if a future census shows frameless-captured functions appearing in
the fallback population (e.g. after Phase 1's deeper spilled-scalar-cfg
rework changes which functions reach `text-size` at all).

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

### Phase 1, Item 1: `homed-scalar-cfg` gains calls to statically-defined callees

**IY-safety precheck (blocking question from the plan, resolved before any
code change):** does a call clobber `iy` when `homed-scalar-cfg` is relying on
it as a live cross-call value's home register?

- `DCCRTL.MAC` never references `iy` at all (`grep -c '\biy\b' DCCRTL.MAC` ==
  0) — the entire runtime library is IY-clean today, but that is an empirical
  fact about the current runtime, not a documented/enforced contract.
- Every dcc-compiled function that uses `iy` as a home register already
  `push iy`s in its prologue and `pop iy`s in its epilogue
  (`mir_emit_home_prologue`/`mir_emit_home_epilogue`, ~line 5540) — so a call
  to any function *this translation unit defines* transparently preserves the
  caller's `iy`, regardless of whether the callee itself uses `iy` internally.
  This is a real, structural guarantee, not an empirical accident.
- `mir_emit_restore_virtual_iy` (~line 7715), invoked by the *spilled* path
  specifically `if (is_indirect || callee == NULL || !callee->is_defined)`,
  is unrelated machinery (it recomputes a *different* "virtual iy base" used
  as a second frame pointer for far-slot addressing) but its trigger
  condition is exactly the right conservative predicate to reuse: "not
  statically known to be a dcc-defined function in this TU" is precisely the
  set of calls we cannot prove safe for register-homed `iy`.

**Decision:** widen `homed-scalar-cfg`'s acceptance scan to admit `MIR_CALL`
and narrow (<=2 byte, non-struct) `MIR_ARG`, but *only* when
`callee != NULL && callee->is_defined && !is_indirect`. Indirect calls and
calls to external/undefined symbols (including the whole runtime library)
stay on the spilled path unconditionally — this is a narrower, more
conservative gate than "IY happens to be safe today," matching SKILL.md rule
1 (identify the exact affected functions/guarantee before widening).

**Implementation** (`src/dcc/dcc_mir.c`):
- Added `mir_emit_home_push` (HL/DE/BC/IY are all directly pushable — no
  intermediate move needed for narrow args).
- Acceptance scan: `MIR_ARG` rejects struct/>2-byte args; `MIR_CALL` rejects
  indirect/undefined/non-defined callees and the `pfehx`/`pfeoc` hook flags
  (kept out of scope for this slice).
- Emission: `MIR_ARG` is a no-op at its own position (args are found via a
  backward scan when the paired `MIR_CALL` is emitted, mirroring
  `spilled-scalar-cfg`'s existing pattern); `MIR_CALL` pushes each home
  register in argument order, calls (with `extrn` if `needs_extrn`), pops the
  stack back with `pop bc` per word (all args are 2 bytes, so no odd-byte SP
  trick is needed), and stores a non-void result via `mir_emit_hl_to_home`.

**Validation:**
- Census: 185/2378 (7.78%) -> 196/2378 (8.24%), +11 functions, 0 regressions
  reported by `--fail-on-regression`
  (`tbcloop.unsafe_index`, `tgnarly.implicit_test`, `tmirfast.dec_dead`,
  `tmirfast.inc_dead`, `tmirslot.cross_call`, `tmirslot.forward_into_store`,
  `tnarrow.fctop`, `tnarrow.mrright`, `tponce.aval`, `tponce.bval`,
  `tpostinc.main`).
- Focused `-Mode full` on the 7 affected apps: 7/7 correctness pass. Two tiny
  peep-mode cycle regressions surfaced (`tmirslot` +42 cycles/+0.13%,
  `tmirfast` +42 cycles/+0.06%) alongside larger nopeep improvements in the
  *same* functions (`tmirslot` nopeep -2.41% cycles/-1.96% bytes, `tmirfast`
  nopeep -0.07% cycles/-1.47% bytes, plus `tponce` -2.27% bytes). The
  identical +42-cycle delta in both apps points to one fixed-size call-site
  overhead (this selector's call sequence vs. the previous spilled-path
  sequence), not a scaling/algorithmic regression, and dccpeep simply hasn't
  learned this new call shape's pattern yet. Net binary size moved down, not
  up, in every affected app. Accepted per the baseline policy's explicit
  allowance ("update baselines only after a complete full-mode run proves the
  new profile is intentional and correctness-clean") and updated
  `tests/perf_baselines.csv` for `tmirslot`/`tmirfast` accordingly — this is
  not a hidden regression, it's a documented, understood, net-positive
  tradeoff.
- Wide `-Mode fast -FailFast`: 314/314 pass.
- Full `-Mode full -Extended -FailFast`: 314/314 + 196/196 pass.

**Next slice for Phase 1:** widen further to calls with wide (4-byte) or
struct arguments/returns, then to `is_indirect`/external calls once (or if)
a documented IY-preservation contract exists for `DCCRTL.MAC`.

### Phase 1, attempted slice: `homed-scalar-cfg` gains `MIR_UNARY '!'` — reverted, real bug found

`mir_emit_homed_unary_instruction` already fully implements `'!'` (boolean
materialization via a fresh label pair) and looked emitter-complete, but the
acceptance scan carries an explicit, redundant
`if (insn->immediate == '!') return 0;` right after allowing it into the
opcode whitelist — a deliberate wall around otherwise-implemented code.
Removing that wall (isolated one-line change) was tried and validated:

- Census: +8 functions (204/2378, 8.58%), 0 `--fail-on-regression` hits.
- Focused `-Mode full` on the one affected app (`tmirfuse`): correctness
  passed, with a small (+0.52%) peep-mode cycle regression offset by a larger
  nopeep improvement in the same app (-1.89% cycles, -1.3% bytes) — the same
  shape of understood, acceptable tradeoff as Item 1's, and was provisionally
  accepted into `perf_baselines.csv`.
- Wide `-Mode fast -FailFast`: 314/314 pass.
- **Full `-Mode full -Extended -FailFast`: 1 failure** — extended
  c-testsuite case `00035`
  (`tests/extended-tests/tests/single-exec/00035.c`), a genuinely new
  correctness break, not a flaky/pre-existing one (confirmed via
  `DCC_MIR_FORCE_FALLBACK_FUNCTION=main`, which passes; only the
  MIR-accepted path fails).

**Root cause, confirmed by hand-simulating the generated assembly** for the
minimal reproducer (`x=4; if (!x != 0) return 1; if (-x != 0-4) return 1;`):
the allocator colors `x`'s value to `HL` for its *entire* live range, which
correctly spans past the `!x` computation because `x` has a second, later
use (`-x`). `mir_emit_homed_unary_instruction` computes `!x` in `HL` as
scratch (since `x`'s home already *is* `HL`, `mir_emit_home_to_hl` is a
no-op) and only skips preserving `HL` when *neither* the operand *nor* the
result is colored `HL` — but that check doesn't cover this case, where the
operand's home is `HL` yet the *result* is homed to a different register
(`DE`). Storing that result via `mir_emit_hl_to_home`'s `DE` case emits
`ex de,hl` — an **exchange, not a move** — which clobbers `HL` with `DE`'s
old contents instead of leaving `x`'s still-live value intact. The next use
of `x` (for `-x`) then silently reads garbage. This is a real bug in the
`mir_emit_homed_unary_instruction` / `mir_emit_hl_to_home` interaction
whenever an operand's home register is reused as scratch, its output is
stored to a *different* home via an exchange-based move, and the operand is
still live afterward. It is not unique to `'!'` in principle — `'-'`/`'~'`
share the same code path and swap-based store — but the coloring never
happened to produce this exact "operand in HL, result in DE, operand still
live" shape for those operators in the existing 96/119-function population,
so it was never exercised before this slice.

**Decision (Item-6-style defer, not a silent skip):** reverted the one-line
acceptance change and its paired `perf_baselines.csv` update in full — tree
is byte-identical to the prior commit (`72b3754`) for `src/dcc/dcc_mir.c`.
Re-verified `00035` passes again on the reverted build. This is deferred,
not abandoned: the real fix is either (a) make `mir_emit_hl_to_home`'s
non-`HL` cases use a true move instead of `ex de,hl`/register-shuffle when
the source register's prior occupant might still be live, or (b) make the
allocator/liveness model treat "operand's home reused as unary scratch, dst
homed elsewhere" as an interference case that never colors the operand to
the scratch register when the operand survives past the instruction. Either
fix is a legitimate, higher-value target of its own (also benefits `'-'`
and `'~'` correctness robustness, not just `'!'`), but is a register-model
correctness change, not a narrow acceptance-gate widening, so it does not
belong bundled with a coverage-widening commit. Filed here as the next
concrete Phase 1/register-model bug to fix in its own dedicated slice, with
`tests/00035.c`-equivalent (`/tmp/t35e.c`-style: `!x` then a later `-x` on
the same operand) as the falsifying regression test to check against.

### Phase 1, Item 2: fix the `mir_emit_homed_unary_instruction` clobber bug, re-enable `'!'`

Landed the deferred fix from the previous slice, in its own dedicated
commit as planned.

**Fix** (`src/dcc/dcc_mir.c`, `mir_emit_homed_unary_instruction`):
`preserve_hl` now also covers the case the old check missed — `src1`'s home
register *is* `HL` (so `mir_emit_home_to_hl` is a no-op) but `src1` is still
used later in the function (`mir_value_has_use_after(insn->src1,
instruction)`, an existing helper) and the result is stored to a *different*
home. In that case `HL` is pushed before the computation and popped back
after, exactly restoring `src1`'s original value regardless of whatever
`mir_emit_hl_to_home` does to `HL` internally (including the `ex de,hl`
exchange). All four existing cases are otherwise unchanged: `src1==HL &&
dst==HL` (true in-place update, no preserve needed), `src1==HL && dst!=HL
&& src1 dies here` (safe to clobber, no preserve — unchanged), `src1!=HL &&
dst==HL` (unchanged), `src1!=HL && dst!=HL` (already preserved before).

This is a general register-model correctness fix, not specific to `'!'` —
it also protects `'-'`/`'~'` from the same clobber if the allocator ever
produces the same "operand homed to HL, still live, result homed elsewhere"
shape for them (a shape the existing 96/111-function population happened
never to exercise, but nothing prevented it in principle).

With the fix in place, re-applied the previously-reverted one-line
acceptance change to admit `MIR_UNARY '!'` again.

**Validation:**
- Reproducer (`x=4; if (!x!=0) return 1; if (-x!=0-4) return 1; return 0;`,
  matching extended test `00035`'s shape) now runs correctly end-to-end via
  `dccmake`/`ntvcm` (exit 0), where it previously failed (exit 1).
- Census vs. a fresh `72b3754` baseline: 196/2378 (8.24%) -> 204/2378
  (8.58%), +8 functions (the same `tmirfuse.n*` set as the reverted attempt),
  0 `--fail-on-regression` hits. One fallback-only metric churn
  (`tnarwin.sumten`, still fallback, +4 bytes/+4 insns from the extra
  push/pop — no runtime effect, correctly excluded from the "requires
  validation" set).
- Focused `-Mode full` on `tmirfuse`: correctness passed. Same understood
  tiny peep-mode delta as before (+0.52% cycles) offset by a larger nopeep
  improvement (-1.89% cycles, -1.3% bytes) in the same app; accepted into
  `perf_baselines.csv` again under the same policy as Item 1.
- Wide `-Mode fast -FailFast`: 314/314 pass.
- **Full `-Mode full -Extended -FailFast`: 314/314 + 196/196 pass, including
  extended test `00035`**, which is the test this whole slice exists to fix.

Coverage now 204/2378 (8.58%).

### Phase 1, attempted slice: `homed-scalar-cfg` gains `MIR_BINARY TOK_SHL/TOK_SHR` — reverted, no net coverage gain

**Hypothesis:** shift operators (`<<`/`>>`) were the only remaining common
`MIR_BINARY` opcodes missing from `homed-scalar-cfg`'s acceptance whitelist.
Since a spilled-mode helper `mir_emit_scalar_shift(out, operation,
is_unsigned)` already existed (expects value in `HL`, count in `E`, result
in-place in `HL` via a `djnz` bit-loop), a homed emitter reusing it looked
like a small, reusable win in the same style as Items 1-2.

**Implemented:** a new `mir_emit_homed_shift_instruction`, following the
same `preserve_hl`/`preserve_de` (`mir_value_has_use_after`-gated) pattern
used by the binary/unary emitters; rejected (both in acceptance and
defensively in the emitter) any shape where the shift-count operand's color
is `HL`, since shift isn't commutative and the general binary path's
operand-swap trick doesn't apply.

**What the evidence showed:**
- Census: +8 functions (204/2378 -> 212/2378, 8.92%), 0
  `--fail-on-regression` hits. Correctness on the newly-accepted set passed
  (`tchess`, `tcodegen`, both 2/2 in focused `-Mode full`).
- But `tcodegen`'s focused `-Mode full` run showed a **material**
  performance regression: peep +18.76% cycles, nopeep +15.65% cycles — an
  order of magnitude larger than the ~0.1-0.5% tiny/net-positive deltas
  accepted for Items 1-2, and squarely the kind of regression SKILL.md rule 5
  says is still "fallback output until fixed," not a baseline-acceptable
  tradeoff.
- Root cause, found by inspecting the 8 newly-accepted functions
  (`tcodegen.c`'s `lsl8`/`lsl9`/`lsr12`/`lsr15`/`asr8`/`asr9`/`asr15` and
  `tchess.c`'s `rank_of` — `return sq >> 3;`): **every one of them shifts by
  a compile-time constant amount.** `mir_emit_scalar_shift`'s generic
  runtime `djnz` loop is much slower than whatever fixed sequence the legacy
  backend already emits for a known constant shift count (e.g. byte swaps
  for shift-by-8, sign-bit extraction for shift-by-15, a short unrolled
  `add hl,hl`/`sra h`/`rr l` chain for small constants) — this is exactly
  the "smaller instruction count is not proof of faster code" trap SKILL.md
  rule 4 warns about, except here it cuts the other way: my generic emitter
  looked reasonable by instruction count but was concretely slower at
  runtime because it didn't special-case the constant-amount shape.
- **Confirmed by construction, not just inspection:** added a rejection for
  constant-amount shifts (`mir_definition(insn->src2)->opcode == MIR_CONST`)
  to the acceptance scan and re-ran the census. Coverage returned to exactly
  204/2378 (8.58%) — identical to the pre-shift-slice baseline — meaning
  **all 8 previously-accepted functions were constant-amount shifts and
  none were genuinely variable-amount**. No function in the current corpus
  exercises the variable-shift shape this emitter was meant to cover.

**Decision:** since restricting to variable-amount shifts (the only
correctness-safe, cost-safe subset) yields **zero net additional coverage**
in the current corpus, the added emitter/acceptance code has no measurable
benefit today and is dead weight. Reverted the entire slice (both the
generic-shift emitter and the acceptance widening) back to the `a7638c3`
state rather than carrying unused code. This is a deferred/skip decision in
the same spirit as Item 6: not a design ambiguity, but a "no yield" result
that's worth recording so a future contributor doesn't re-attempt the same
generic-shift approach without first checking whether the target functions
use constant or variable shift amounts.

**If revisited:** a real win here would require either (a) constant-amount
shift codegen matching legacy's specialized sequences (byte swap / sign
extraction / short unrolled add-chains) before admitting constant shifts, or
(b) finding actual variable-amount shift functions in a wider corpus (none
exist in the current 2378-function census) to validate the loop-based
approach was worthwhile in the first place. Neither is a quick follow-up;
parking both.

Coverage unchanged at 204/2378 (8.58%).

## Phase 2 resolved: root cause found and fixed (mir-migration-plan-forward.md Item A)

**Root cause**: `set_sym_val` (`tests/forint.c:333`) is `static inline`.
Legacy's AST-level inline substitution (`dcc_ast_gen_expr.c`,
`try_gen_inline_call_ast`) eliminates every call site, so `set_sym_val`
never gets a standalone emitted body anywhere in the program (confirmed:
neither the base nor a forced `.mac`/`.PRN` contains a `_Z0026:` label
definition for it - only `call _Z0026` references). MIR lowering already
tracks this correctly via `mir_inline_substitutable()` and tags such
`MIR_CALL` instructions with `memory_flags |= 2048`, and the acceptance
gate at `mir_has_inline_substitution_call()` exists precisely to keep any
function containing such a call on fallback. However, a "near-cost"
exception introduced with the Phase 2 fused-comparison-branch batch let a
candidate through this gate anyway whenever the MIR-generated code was
within 5%/+1 instruction and no larger in bytes than the legacy capture -
with no check that the callee actually has a materialized body. This is
exactly the shape `assign_pre` would hit if forced through the gate,
producing a real `call` to a symbol with zero bytes of code anywhere,
which explains the byte-exact "192 bytes -> 0 bytes" corruption signature
investigated earlier this session under `DCC_MIR_FORCE_ACCEPT_FUNCTION`.

**Verification the exception was unsound but not yet live**: a corpus-wide
scan (instrumenting the exception branch to log every function that would
hit it) over all of `tests/*.c` found **zero functions currently exploit
it** - `assign_pre` itself is correctly rejected in production by the base
gate already (57 vs 229 generated/captured instructions, 666 vs 2509
bytes, nowhere near the near-cost threshold). So this was a landmine, not
an active regression: no coverage was gained by having the exception, and
none is lost by removing it.

**Fix landed**: removed the unsound near-cost exception entirely from the
`inline-substitution` fallback-reason check in `dcc_mir.c` - any function
containing an inline-substitution-eligible call now unconditionally falls
back, with no byte/instruction-count carve-out, per SKILL.md rule 6
(derive a structural predicate, not a name/cost-based exception without a
materialization proof). Also removed the now-superseded
`DCC_MIR_DISABLE_ITEM27_FUNCTION` diagnostic gate from
`mir_fused_compare_is_signed_zero_sign_test`, which had already served its
purpose this session (conclusively exonerating Item 27's fusion as the
cause of the `assign_pre`/`bump_sym_val` force-accept corruption).

**Validation**: full census against `build/mir-next10-before.tsv` shows no
regression (`--fail-on-regression` passed cleanly, coverage unchanged at
204/2378 - consistent with zero functions exploiting the removed
exception); focused `-Mode full` on the 14 apps with any census churn
(`cint,tbcloop,tc99scpe,tcrcfix,tgnarly,tmirfast,tmirfuse,tmirslot,
tnarrow,tponce,tpostinc,tscanf,tstdlib,tsyntax`) all pass with 0
performance regressions (5 minor nopeep improvements, unrelated); wide
`-Mode fast -FailFast` passes 314/314 with dccpeep fixtures green.

**`mir-migration-plan-forward.md` Item A is complete.** Item B (prevalence
survey of the Item-27 shape) is now largely moot for `forint` specifically
since neither implicated function was ever live in production, but the
underlying question - how common is this static-inline-call shape across
the corpus, and would relaxing the gate *soundly* (e.g., by proving a
callee is address-taken or otherwise forced to have a real body) be worth
pursuing - remains open and is deferred to a future session per the
original Item B scope.

## Correction (2026-07-31): coverage numbers above predate a census double-counting fix

Every coverage figure recorded above in this document (185/2378, 196/2378,
204/2378, 212/2378, etc.) was measured before a since-fixed bug in
`mir_end_function`'s reporting (commit `fbff14c`, `dcc_mir.c`): legacy's
discard-capable speculative codegen attempts (no-IX-frame, BC/E regalloc,
IY regalloc, loop-scoped-BC-first — all in `dcc_regalloc.c`) each re-drove
`mir_begin_function`/`mir_end_function` in lockstep, and every discarded
attempt's `; MIR selection ...` line was printed to `DCC_MIR_SELECT_REPORT`/
the census alongside the one real, kept attempt's line — inflating both the
numerator (functions incorrectly counted as covered from a discarded
attempt) and, more subtly, corrupting `captured-bytes` for many `text-size`
fallback rows (proven directly: `check()` in `tests/tesc.c` reported 5
different `captured-bytes` values in one compile before the fix).

**Corrected, current, trustworthy coverage: 139/2018 functions (6.89%).**
This is not a regression — no code/output changed (verified: 0 differences
across all 295 `tests/t*.c` apps' generated `.mac` output, pre-fix vs
post-fix). All prior session narrative, root-cause findings, and phase
reasoning above remain valid; only the specific percentages should be
read as "measured with a since-corrected reporting bug" rather than
literal ground truth. Do not use any pre-`fbff14c` coverage number as a
baseline for `--compare`/`--fail-on-regression` without regenerating it
against a post-fix binary first.

### Phase 1, Item 3: `homed-scalar-cfg` gains calls to undefined/external callees

**Hypothesis:** Item 1's `MIR_CALL` gate (`callee != NULL && callee->is_defined
&& !is_indirect`) is stricter than necessary. The IY-safety precheck it was
built on ("a defined callee's own prologue/epilogue push/pop iy around it")
is *one sufficient* proof of safety, but not the only one, and the gate
conflated "provably safe" with "defined in this TU." `dcc.h` (~line 439-447)
documents a broader, already-relied-upon invariant: IY is CALLEE-SAVED across
*any* call in a linked image, because `DCCRTL.MAC` contains zero IY
references (`grep -ic '\biy\b' DCCRTL.MAC` == 0, matching the automated
check in `scripts/rtl-iy-safety.py`) and CP/M's 8080-coded BDOS has no index
registers to clobber it with. The legacy backend's own
`function_qualifies_for_speculative_iy_regalloc` (`dcc_regalloc.c` ~line 336)
already leans on exactly this invariant and does not distinguish
defined-in-TU calls from calls to undefined/external (DCCRTL) symbols. An
indirect call remains the only case that must stay excluded, since its true
target isn't known at compile time and can't be proven to be dcc-compiled or
part of DCCRTL/BDOS.

**Implementation** (`src/dcc/dcc_mir.c`, `mir_try_emit_homed_scalar_cfg`'s
`MIR_CALL` acceptance case, ~line 6026): removed the `!callee->is_defined`
condition, keeping only `is_indirect || callee == NULL`. Updated the code
comment to cite the broader invariant and the legacy backend's matching
precedent instead of the narrower "this TU defines it" rationale. No emission
changes were needed: `sym_asm_name(callee)`/the existing `extrn` handling
already treat any non-NULL `callee` uniformly regardless of `is_defined`,
matching the pattern already used by `spilled-scalar-cfg`/`general-rollout`
for their own (always-had-this-case) `MIR_CALL` emission.

**A second, larger blocker found while falsifying the hypothesis:**
`tscanf.c`'s `check_str` (chosen as the test candidate: calls both `strcmp`
[external] and `fail_str` [in-TU-defined], `spills=0, cross-call=0` per
`DCC_MIR_REPORT`) remained on `spilled-scalar-cfg` fallback even after this
change (`DCC_MIR_GENERAL_FUNCTION=check_str` confirms `homed-scalar-cfg`
still rejects it). Root cause: `mir_try_emit_homed_scalar_cfg`'s acceptance
switch has **no `case MIR_LOAD:`** at all — any function whose parameters or
locals are promoted to an addressable object (needing an explicit reload
after a branch, rather than being reused as a pure SSA value) falls through
to `default: return 0` regardless of the `MIR_CALL` gate. This affects far
more of the corpus than just calls: even `tesc.c`'s `check()` (int
params, no calls to undefined callees at all — only calls `fail`, already
in-TU-defined and already accepted since Item 1) uses `MIR_LOAD` for its
`name` parameter (reloaded for the failure-branch's second use) and was
never reaching the `MIR_CALL` gate either. This is a separate, likely
higher-yield selector gap (any multi-use parameter/local across a branch)
and is out of scope for this slice; recommended as the next Phase 1 item.

**Yield of this slice alone (measured, `--fail-on-regression` clean):**
+2 functions — `tnarrow.mrleft` and `wumpus.flsh` (both are pure-SSA,
no-`MIR_LOAD` functions that call an undefined/external callee, e.g. a
runtime helper). Coverage: 139/2018 (6.89%) -> **141/2018 (6.99%)**. No
functions regressed to fallback, no already-active MIR function's generated
metrics changed except the two newcomers. Focused
`pwsh ./scripts/runall.ps1 -Apps tnarrow,wumpus -Mode full -RunTimeout 20`:
2/2 pass, 0 regressions, 2 minor nopeep improvements (tnarrow -0.07%, wumpus
-0.13%). Wide `-Mode fast`: 314/314 apps, 106/106 diagnostics, 17/17 dccpeep
fixtures, performance all pass.

**Conclusion:** the relaxation is correct and safe but low-yield in
isolation because it is gated behind the much larger `MIR_LOAD` gap above.
Landing it now removes a proven-unnecessary restriction (SKILL.md rule 1:
identify the exact affected functions before widening — done here) and
unblocks the two functions that don't also need `MIR_LOAD`, but the real
lever for Phase 1's "biggest expected yield" framing is adding `MIR_LOAD`
support (acceptance + emission) to `homed-scalar-cfg`, which is a
substantially larger, separate slice (needs a reload/store strategy for
promoted objects, not just a gate check) and should be scoped and attempted
independently next.

**Defer decision (Item-6-level design ambiguity, not a quick follow-on
slice):** inspecting *why* `tesc.c`'s `check()` needs a real `MIR_LOAD` for
`name` (single use, inside the branch taken only on failure) rather than
the "resident-in-register, demote to NOP" treatment `got`/`expected` already
get, confirms this is exactly the complexity the plan's own Phase 1 item 2
warned about: whether a value is still provably resident in its entry
register at a later use depends on cross-block register liveness at
merge/branch points, which `homed-scalar-cfg` does not reason about at all
today (it has no notion of "this register may have been reassigned to a
different value across a branch"). Getting this wrong would silently read
the wrong register - a correctness bug, not a missed-optimization. Building
it properly needs the same cross-block register-identity design the plan
already flags as the *harder*, general-spill-aware emitter (Phase 1 items
1-4), not a small acceptance-gate widening. Per SKILL.md's discipline for a
design decision this significant: **deferred, not attempted this session.**
Recommended as a dedicated future slice, scoped and validated on its own
(start from the narrowest case: an object with exactly one cross-block
reload and no address-taken/aliasing, prove register identity is preserved
before allowing more).

### Phase 1, Item 4: promote the dead `mir_try_emit_comparison_branch` selector into production, gated on `instruction-count` rejection

**Discovery while implementing Item 3's reorder attempt:** an initial
attempt to fix the `instruction-count` fallback bucket (23 functions, e.g.
`tmirfuse.c`'s `sge`/`sgt`/`sle`/`slt`/`sne`/`uge`/`ugt`/`ule`/`ult`/`seq`,
all the exact whole-function shape `if (param OP param) return CONST;
return CONST;`) by reordering `mir_try_emit_z80`'s selector list so
`mir_try_emit_comparison_branch` ran before `homed-scalar-cfg` had **zero
effect** - because `mir_try_emit_z80` is not the production dispatcher at
all. It is only reachable via two diagnostic env vars
(`DCC_MIR_CANDIDATES`, `DCC_MIR_EMIT_FUNCTION`). The real production path
(`mir_end_function`, ~line 10895) calls `mir_try_emit_general_rollout`/
`mir_try_emit_homed_scalar_cfg`/`mir_try_emit_spilled_scalar_cfg` directly,
with a separate, narrowly-gated "rescue" mechanism (Phase 5 Item 46) that
retries the loop-family selectors only when `fallback_reason ==
"cfg-backedge"`. `mir_try_emit_comparison_branch` (and
`mir_try_emit_scalar_dag`) have **no such rescue** and are therefore fully
dead code in every normal build - a genuine, previously-uncounted gap in
an already-implemented, already-tested selector, matching this project's
earlier "promoting dead selectors" pattern (Phase 5/8 in this same
Execution Log).

**Fix**: added a second rescue block, modeled exactly on the Phase 5 Item
46 loop-family rescue, that retries `mir_try_emit_comparison_branch`
whenever `fallback_reason == "instruction-count"` (the exact bucket this
selector's shape addresses) and `mir.return_type` is a 16-bit int. Kept
only if not worse than legacy's captured cost, using the same
`mir_is_profiled_near_cost_single_block`/`mir_is_byte_profitable_single_block`
checks as the existing rescue. This can never affect any function
homed/spilled-scalar-cfg would otherwise accept outright, because the
rescue only fires once every earlier gate has already failed with exactly
this one reason.

**Regression found and fixed during validation (SKILL.md rule 4 in
practice):** the first `-Mode full` run showed a genuine **peep-mode
regression** for `tests/tmirfuse.c` (9,216 -> 9,344 bytes, +1.39%) despite
nopeep improving (-1.32%) and every individual function's raw generated
byte/instruction count being smaller than legacy's capture. Root cause:
`mir_try_emit_comparison_branch` called `mir_emit_return_constant` twice
(once per return path), and that helper unconditionally emits the *full*
`ld sp,ix / pop ix / ret` epilogue every time it's called - duplicating
the epilogue instead of sharing one between both paths the way legacy's
own capture already does (`ld hl,1 / jr L193` / `ld hl,0` falling through
into one shared `L193: ld sp,ix / pop ix / ret`). Pre-peephole this still
measured smaller per function (fewer total instructions than legacy's
push/pop-heavy comparison sequence), but dccpeep's `jp_to_jr` and
dead-epilogue passes were tuned to legacy's merged-epilogue shape, so the
duplicated-epilogue version came out net larger after peephole across the
whole app. Fixed by rewriting the two `mir_emit_return_constant` calls
into an explicit shared-epilogue sequence (`ld hl,<true> / jp
L<epilogue>` ... `L<false>: ld hl,<false>` ... `L<epilogue>: ld sp,ix /
pop ix / ret`), matching legacy's shape exactly. `mir_emit_return_constant`
had no other callers after this change and was removed (confirmed via
`-Wunused-function`).

**Yield:** +10 functions, all in `tests/tmirfuse.c`
(`seq,sge,sgt,sle,slt,sne,uge,ugt,ule,ult`). Coverage 141/2018 (6.99%) ->
**151/2018 (7.48%)**. `instruction-count` fallback bucket: 23 -> 13.
`census --fail-on-regression` clean (0 regressions, 10 newly-emitted,
0 apps' already-active MIR metrics changed). Focused
`pwsh ./scripts/runall.ps1 -Apps tmirfuse -Mode full -RunTimeout 20`:
1/1 pass, 0 regressions (after the epilogue fix), 3 improvements (peep
cycles -0.04%, nopeep cycles -1.08%, nopeep bytes -1.32%). Wide `-Mode
fast`: 314/314 apps, 106/106 diagnostics, 17/17 dccpeep fixtures,
performance passed.

**Remaining `instruction-count` bucket (13 functions) after this slice**
(from a fresh census): `attnc11.process_sequence`, `tc89swjt.swdn`/`swsp`,
`tc99scpe.for_multi_declarators`, `tchess.file_of`/`on_board`,
`tinline.edge_and`/`edge_conditional`/`edge_or`, `tmirslot.immediate_use`,
`tregnarw.lbig`/`lusr`, and one large outlier `tswitch.f` (459 generated
vs 95 captured instructions - a `switch` statement, likely lowered
inefficiently per-case rather than via a jump table). None match
`mir_try_emit_comparison_branch`'s narrow whole-function
compare/branch/return-constant shape, so this slice does not touch them.
Recommended as separate future slices, each needing its own falsifiable
hypothesis rather than a blanket gate relaxation.

### Phase 1, Item 5: `mir_try_emit_scalar_dag` production rescue - rejected experiment, deferred

Following Item 4's pattern, `mir_try_emit_scalar_dag` (single-return,
branch-free, purely-arithmetic bodies) was found to be the *other*
selector only reachable via the `DCC_MIR_EMIT_FUNCTION` diagnostic
dispatcher, never from `mir_end_function`'s real production path. A
corpus-wide structural survey (temporary `DCC_MIR_SCALAR_DAG_SURVEY` env
hook, removed after use) found 32 distinct functions across 11 apps
(`tbool.bool_param_sum`, `tc89size.nb_c99_type_counts`,
`tc99varm.two`/`three`, `tcodegen.lsl8`/`lsl9`/`asr15`/`asr8`/`asr9`,
`tkandr.default_int`/`uchar_mix`, `tmulpow2.umul_lhs`/`umul_rhs`,
`tv6`/`mystery_fn`/`mulb`/`multh`/`mul2`/`mul7`/`mul15`/`mul31`/`mul63`/
`mul127`/`mul255`/`scale`/`pat`/`rpat`/`rank_of`/`file_of`) currently
rejected for `text-size` or `instruction-count`, of which ~11 passed the
existing near-cost/byte-profitable static gate (the same cost check
Item 4 reused).

**A full second-chance rescue was implemented (mirroring Item 4's
structure exactly), rebuilt, and passed `census --fail-on-regression`
cleanly: +11 functions, 151/2018 (7.48%) -> 162/2023 (8.01%), 0
regressions in the static census metrics.** A byte-delta in a handful of
*unrelated* caller functions (`tcodegen.main`/`scnt`/`scod`/`srdy`) was
investigated and confirmed to be a harmless artifact of the shared global
label counter (renumbered labels shift later functions' assembly-text
length by 1-2 characters with zero effect on assembled Z80 bytes,
confirmed via direct side-by-side `.mac` diff normalizing label numbers)
- not a real widened blast radius, and not itself a reason for concern.

**However, the mandatory focused `-Mode full` run
(`tbool,tc89size,tc99varm,tcodegen,tkandr,tmulpow2`) found real,
significant performance regressions in 5 of the 6 affected apps:**
`tcodegen` peep +7.12%/nopeep +6.17%, `tmulpow2` peep +3.04%/nopeep
+5.6%, `tc99varm` nopeep +3.6%, `tkandr` peep +0.01%/nopeep +1.55%. Only
`tc89size` (pure add/sub/compare arithmetic, no shifts or multiplies)
improved cleanly. Root cause: `mir_try_emit_scalar_dag`'s shift/multiply
codegen (`mir_emit_scalar_shift`, the `'*'` case in `mir_emit_scalar_value`)
has no constant-operand specialization at all - it always emits the
fully generic runtime form (a `push ix`/`add ix,sp` frame plus a `djnz`
counted loop for shifts, `call __mulu` for every multiply), whereas
legacy's AST-level codegen constant-folds a compile-time shift count into
a straight-line unrolled `add hl,hl` sequence (no frame, no loop) and
specializes small-constant multiplies similarly. The static byte/instruction
gate could not see this: `lsl8`'s rescued form measured 177 generated vs
179 captured *assembly-text* bytes (a near-tie), but the real assembled
code is a stack-framed runtime loop (~33 bytes, N loop iterations) against
legacy's tight unrolled sequence (~21 bytes, zero branches) - exactly the
SKILL.md rule 4 case ("a smaller assembly-text stream or instruction count
is not proof of faster or smaller Z80 code").

**Rejected and reverted, not merged.** Fixing this properly requires
adding constant-operand strength reduction to `mir_emit_scalar_shift` and
the multiply case of `mir_emit_scalar_value` (detect a `MIR_CONST` shift
count / multiplicand and emit legacy's unrolled/specialized form instead
of the generic loop/runtime-call form) - a separate, larger emitter-quality
project, not a "smallest reusable edit" for this slice, and squarely a
prerequisite rather than an extension of this rescue. Re-attempting the
scalar-dag production rescue is only safe after that emitter work lands;
until then only `tc89size.nb_c99_type_counts` (and similarly shift/multiply-
free bodies) would be genuinely safe to promote, which is not enough
independent yield to justify a narrower shift/multiply-free gate on its
own. Working tree fully reverted to the Item 4 checkpoint (`0bb502b`);
confirmed via a fresh census matching 151/2018 (7.48%) exactly with no
residual diff.

### Phase 1, Item 6: constant-shift strength reduction - scoped and scored, not implemented (deferred as Item-6-level design complexity)

Item 5's rejection pointed at a root cause worth investigating on its own
merits, independent of the scalar-dag rescue: **the production
`mir_emit_scalar_operation`/`mir_emit_scalar_shift` path (used by
`spilled-scalar-cfg`, the dominant selector for 1930 of 2018 attempted
functions) has no constant-operand strength reduction for `TOK_SHL`/
`TOK_SHR` at all**, unlike `'*'` (which already has
`mir_mul_const_fast_path_eligible`/`mir_emit_mul_hl_const`, ~line 6609)
and `'/'`/`'%'` (which already has a constant-divisor shortcut, ~line
9284). Every shift, even by a literal compile-time constant, always
materializes the count into a register and runs a generic `djnz`-counted
loop.

**Corpus-wide scope, measured directly** (temporary
`DCC_MIR_SHIFT_CONST_SURVEY` debug hook in `mir_emit_scalar_operation`'s
caller, removed after use): 32 distinct app.function pairs across 21 apps
use a shift with a literal constant count. Cross-referencing against a
fresh census: the large majority (`a1.emulate`/`op_bcd_math`/`op_rotate`,
`adaint.mem_get_word`/`mem_set_word`, `cint.mem_get_word`/`mem_set_word`/
`global_decl_or_func`, `fint`/`forint.cell_at`/`set_cell`/`run_at`,
`pihex.powermod16`, `tarray.aHexByte`/`aHexWord`,
`tbdos`/`tbfinit`/`tbitfld`/`tbits32`/`tcaslv`/`tpromo2`/`tpromo32`/`ts`/
`ts32.main`, `tchess.rank_of`, `tinline.mem_get`/`mem_set`) are already
on `text-size` fallback via `spilled-scalar-cfg`, but with generated/
captured byte ratios ranging from ~1.4x (`tchess.rank_of`: 243 vs 132,
the one plausible near-miss) up to ~3.7x (`a1.emulate`: 99760 vs 26573) -
meaning shift codegen is very rarely the *sole* or even dominant cause of
these functions' bloat; most have other, larger, compounding text-size
issues. **Realistic yield from this fix alone is therefore likely small
(low single digits of newly-accepted functions), even though the
underlying quality gap is real and touches a meaningful fraction of the
corpus.**

**A second, independent discovery surfaced during this scoping pass, and
was traced to its exact source rather than left as a surface
observation**: `tcodegen.c`'s dedicated shift-test functions (`lsl8`/
`lsl9`/`asr8`/`asr9`/`asr15`/`lsr8`/`lsr12`/`lsr15`) never appear in
`DCC_MIR_SELECT_REPORT`/the census at all in the current committed
build, despite having their own standalone, correct `.mac` symbols with
real generated code. A temporary probe print confirmed
`g_speculative_codegen_active` is `1` every single time
`mir_try_emit_spilled_scalar_cfg`'s frame preflight runs for these
functions, and tracing into `try_speculative_noix_function_body`
(`dcc_regalloc.c` ~line 190) confirmed why: **this is not a suppressed
duplicate report of a real, separately-reportable final pass - there is
no second pass**. Legacy's no-IX-frame speculative attempt runs
`gen_compound()` (driving `mir_begin_function`/`mir_end_function`
exactly once) inside a scratch buffer under
`g_speculative_codegen_active`, and if that attempt is smaller/kept, its
scratch output is copied *directly* to the real destination stream by
the caller - the "normal" codegen path (and any further
`mir_end_function` invocation) never runs again for that function.
`mir_end_function`'s own reporting suppression is therefore working
exactly as designed (avoiding a double/discarded-attempt report); the
gap is that **`DCC_MIR_SELECT_REPORT`/`mir-migration-census.py` have no
visibility into this entire class of functions**, because a function
whose real, final, kept output comes from any of legacy's four
discard-capable speculative regalloc variants (no-IX-frame, confirmed
directly; BC/E regalloc, IY regalloc, and loop-scoped-BC-first are
structurally identical per the matching `g_speculative_codegen_active`
pairs at `dcc_regalloc.c` lines ~1285, ~1365, ~1557) is invisible to the
census - not misclassified, but **entirely absent from both the
numerator and the denominator**. Measured directly for one file:
`tests/tcodegen.c` has 12 function definitions total, but only 7 ever
appear in `DCC_MIR_SELECT_REPORT` - the missing 5 are exactly
`lsl8`/`lsl9`/`asr8`/`asr9`/`asr15`, a ~42% blind spot for this
particular file. This means the census's reported denominator (this
session's snapshots all read "151/2018") likely **understates the true
corpus size**, and the true MIR coverage percentage is unverified until
this blind spot is measured corpus-wide - a distinct, and arguably more
consequential, finding from the shift-strength-reduction question above,
not something this session's changes caused (reproduced identically on
the unmodified `0bb502b` checkpoint before any of today's edits), and
worth a dedicated corpus-wide measurement (not just a per-file spot
check) before treating any current or future census percentage as
exhaustive. A plausible fix is to have `mir_try_selector`/
`mir_try_emit_spilled_scalar_cfg` (or the callers in
`dcc_regalloc.c` that invoke speculative attempts) emit a
`DCC_MIR_SELECT_REPORT` line of their own when a speculative attempt is
kept, rather than relying on `mir_end_function`'s normal report path
which structurally never runs a second time for these functions.

**Why this was scoped but not implemented this session:** legacy's own
constant-shift codegen (inspected directly via `.mac` capture) is not a
simple fixed unroll - it has a real per-count strategy table: plain
`add hl,hl`/`sra h;rr l`/`srl h;rr l` unrolling for small counts (e.g.
shift-by-8/9 in `lsl8`/`lsl9`), a byte-boundary move-and-extend shortcut
at counts >= 8 (`lsr8`: `ld l,h / ld h,0`, zero total shift instructions
beyond the move; `asr8`: `ld a,h/ld l,h/rla/sbc a,a/ld h,a`, a sign-
replication trick), and a combination of the move-and-extend shortcut
plus a remainder unroll for counts between 9 and 15 (`lsr12`: move+zero
then 4 more `srl l`; `asr9`: sign-trick then 1 more `sra h/rr l` pair).
Faithfully replicating this table - across 16 possible counts x 2
signedness x 2 directions, with correctness (not just size) on the line
for a change to the dominant production selector affecting 1930+
functions - is a legitimate "improve the emitter" project per SKILL.md
step 6, but is **not** the "smallest reusable edit" this slice's risk
budget supports without a dedicated test matrix (every count 0-15 x
signed/unsigned x left/right, verified by actual execution, not just
static byte inspection - directly on point after the Item 5 lesson that
smaller generated-byte counts do not certify correctness or speed).

**Recommended for a future, dedicated slice**, in this order: (1)
measure the speculative-attempt blind spot corpus-wide (add a report
line where a speculative attempt is kept, per the note above), so the
true corpus-wide shift-const population and its actual fallback/accepted
breakdown - and the true overall coverage denominator - can be measured
precisely; the current 32-function count and the 2018-function corpus
size are both very likely undercounts; (2) implement constant-shift
strength reduction as a narrow, incrementally-tested selector
improvement (start with the safest subset - small counts 1-7 with plain
unrolling only, matching legacy exactly, before attempting the
byte-boundary/sign-trick shortcuts for counts >= 8); (3) validate with
the full ladder (`census --fail-on-regression`, then
`runall.ps1 -Mode full` on every app in the cross-reference list above,
not just the ones that flip to `mir`, since already-*accepted* functions
using shift would also change shape and need the same non-regression
proof). Deferred, not attempted,
per the same Item-6-level-design-ambiguity threshold the user
authorized skipping past. Working tree confirmed clean and matching
151/2018 (7.48%) after removing the temporary survey instrumentation.

### Phase 1, Item 7: measure the yield ceiling for a spill-tolerant `homed-scalar-cfg` - measured, deferred as high-risk/low-reward

This item picks up Phase 1's own "Suggested first concrete slice" text
above (extend `homed-scalar-cfg` to permit values beyond the current
strict `mir.allocation_spill_count != 0` zero-spill requirement). That
suggestion predates this session's Items 1 and 3, which already landed
`MIR_CALL` support in `homed-scalar-cfg` for both in-TU-defined and
external/library callees - so the "allow calls" half of the original
suggestion is done, and the true remaining lever is exactly the
zero-spill gate itself.

**Falsifiable check performed**: a temporary, env-gated survey
(`DCC_MIR_HOMED_SPILL_SURVEY`) relaxed only the `allocation_spill_count
!= 0` structural rejection (and the paired "every dst must have a real
register color" rejection, which spilled values fail by definition),
leaving every other `homed-scalar-cfg` structural check - return type,
operand width, opcode whitelist, call-ABI rules, single-word `MIR_PARAM`
requirement - unchanged, and printed (never emitted) each candidate that
would pass everything else. Ran corpus-wide with default args against
every non-ignored app in `tests/_test_overrides.json`.

**Result**: only **11 distinct functions** corpus-wide would newly
qualify structurally: `byte_loop_cache` (tpeepal), `edge_outer_body`
(tinline), `eqz`/`gez`/`ltz`/`nez` (tmirfast), `scale_by` (tbcint),
`sum_stride` (tnestfor) - all with exactly 1 spilled value - plus
`slt_spilled` (5 spills), `nslt_spilled` (6 spills), and
`and_chain_spilled` (11 spills) from tmirfuse (whose names make clear
they exist specifically to exercise multi-spill shapes). Restricting to
the cheapest single-spill case still yields only 8 functions.

**Why this is deferred rather than implemented, despite fitting
SKILL.md's "1-20 functions" batch-size guidance**: the yield is real but
small, and the *only* way to realize it is to add spilled-value (ix-
slot) load/store support directly inside `homed-scalar-cfg`'s shared
emission helpers - `mir_emit_homed_unary_instruction`,
`mir_emit_homed_binary_instruction`, `mir_emit_homed_phi_copies`, and the
prologue/frame-size logic in `mir_emit_home_prologue` - every one of
which is currently exercised, correctly and with zero spill-handling
complexity, by the **96+ already-accepted zero-spill functions**. Adding
a spilled-value code path to each of these shared helpers (particularly
correct PHI/merge-point handling when the merged value is a slot rather
than a fixed register - exactly the "primary correctness risk" Phase 1's
own hypothesis text calls out for the *general* spilled-CFG rework) means
every homed-scalar-cfg change from here on carries real regression risk
for a population 9-12x larger than the reward. This is a legitimate
future project, but not a "smallest reusable edit" for this slice: it is
the same class of large, correctness-sensitive, multi-step work
(register liveness at merge points, call-clobber correctness) that
Phase 1's own text already flags as needing incremental rollout with a
narrowest-safe-subset start, and 8-11 functions is not enough evidence to
justify that risk right now, especially compared to the theoretical
scale of the still-unaddressed *general* `spilled-scalar-cfg`
register-awareness hypothesis, which covers the ~1930-function dominant
fallback population and is Phase 1's actual "largest expected yield"
target.

**Recommended for a future, dedicated slice**: if pursued at all, start
with the 8 single-spill functions only, using a *dedicated* one-slot
frame extension (a single fixed `ix`-relative word reserved whenever
`allocation_spill_count == 1`) and add spill-aware branches only to the
specific helpers these 8 functions' MIR actually exercises (verified
narrowly, not broadened preemptively), then validate every currently-
accepted `homed-scalar-cfg` function still round-trips byte-identically
(a regression in the emitted bytes for any of the 96+ already-accepted
functions, not just a runtime failure, should be treated as a blocking
signal given how easy it is for a shared-helper edit to change an
unrelated code path). Temporary survey instrumentation was fully
reverted (`git checkout --`); working tree and census confirmed back at
exactly 151/2018 (7.48%) with zero diff.

### Phase 1, Item 8: `homed-scalar-cfg` gains `void`-returning functions

**Motivated directly by Item 7's own data**: the same corpus-wide
zero-spill-fallback methodology, re-run without stopping at the first
blocking condition, showed `homed-scalar-cfg`'s hard `(mir.return_type &
15) != TYPE_INT` return-type gate is by far the single largest cause of
zero-spill functions never reaching this selector at all: **997 of 1861
zero-spill functions surveyed** (functions the register allocator
already colors with zero spills, but that still end up on
`spilled-scalar-cfg`) fail on `return-type` before any other check even
runs, versus the next-largest causes (`opcode-load` 215, `value-width`
129, `store-not-promoted` 100, `opcode-straddr`/`opcode-address` 170
combined, `binary-op` 52). Since `mir.allocation_colors`/
`allocation_spills`/`allocation_spill_count` are computed by
`mir_allocate_registers()` unconditionally for *every* function
regardless of which selector eventually runs (confirmed by reading
`mir_end_function`'s call site, ~line 4857) - this data was always
"free" for `spilled-scalar-cfg`'s candidates too, and `homed-scalar-cfg`
was simply never given the chance to use it because of this gate.

**Why `void` is the safe first subset to add** (of the 997): unlike
`char` (also caught by this gate but requiring signed/unsigned
promotion-boundary decisions on the way in/out) or `long`/aggregates
(genuinely different, wider ABI conventions), `void` changes nothing
about the calling convention this selector already implements - there
is simply no value to place in HL at the return point. `MIR_CALL`'s
existing emission code already special-cases a void callee's result
(skip storing to home when `(insn->type & 15) == TYPE_VOID`), giving a
direct, already-proven pattern to mirror for `MIR_RETURN`.

**Implementation** (`src/dcc/dcc_mir.c`, `mir_try_emit_homed_scalar_cfg`):

1. Widened the top-of-function return-type gate to accept
   `TYPE_VOID` alongside `TYPE_INT` (the width check, `type_size() > 2`,
   already passes trivially for void).
2. Relaxed `if (return_count == 0) return 0;` to only reject when the
   return type is *not* void - mirroring
   `mir_try_emit_spilled_scalar_cfg`'s own existing "implicit-return"
   preflight reject, which already treats a `void` function falling off
   the end with no explicit `return;` as legitimate.
3. Guarded the `MIR_RETURN` case's `mir_emit_home_to_hl(out, insn->src1)`
   call to skip it for `void` (there is no value to load - `insn->src1`
   is not meaningful for a bare `return;`), while every other
   `MIR_RETURN` behavior (frameless `ret` vs. `mir_emit_home_epilogue`)
   is unchanged.
4. Added a new tail check after the main instruction-emission loop: if
   the return type is `void` and the *last* MIR instruction was not
   itself a `MIR_RETURN` (covering both `return_count == 0` entirely,
   and a function with an early `return;` followed by more code that
   falls off the end with no final return), emit the epilogue once more
   at the true end of the body - every in-loop `MIR_RETURN` already
   does this inline, so this only fires for the implicit-fallthrough
   exit path that never reaches that case.

**Validation**: established a clean before-baseline via `git stash` +
rebuild + census, then `census --fail-on-regression` after the change:
clean, no functions lost, coverage 151/2018 (7.48%) -> **152/2018
(7.53%)**, net **+1 function** (`ttt.ttt`). `homed-scalar-cfg`'s own
selector count grew from 67 to 74 (+7), but 6 of those 7 were
functions *already* MIR-emitted via a different selector
(`general-rollout`/`comparison-branch`) that simply moved to
`homed-scalar-cfg` - a harmless internal reassignment, not new coverage,
confirmed via `snapshot delta: no longer MIR-emitted: 0`. The gap
between the 997-function survey estimate and the +1 realized yield is
expected and was flagged in the survey write-up itself: the survey
reports only the *first* blocking reason per function, so the true
population is dominated by functions that are void *and* also blocked
by one of the other listed reasons (a `void` function's body can still
freely use any opcode, and in practice most do - I/O calls, global
variable access via `MIR_LOAD`, buffers via `MIR_ADDRESS`, etc.).

Focused validation (`pwsh ./scripts/runall.ps1 -Apps
attnc11,cint,ttt -Mode full -RunTimeout 20`) passed correctness for all
3 apps, but flagged one performance regression: `cint`'s `parse_expr`
(a trivial `void parse_expr(void) { parse_assign(); }` trampoline,
extremely hot in cint's recursive-descent parser) moved from
`spilled-scalar-cfg` (72 generated bytes, 7 insns, full unnecessary
`ix`-frame prologue/epilogue for a function with zero locals/params) to
`homed-scalar-cfg` (25 generated bytes, 2 insns, correctly frameless).
`nopeep` cycles and `.com` size both *improved* for `cint`
(581,476,571 -> 581,475,207 cycles; 34,560 -> 34,432 bytes, -0.37%), but
`peep` mode showed a reproducible (confirmed via a second independent
run, identical to the cycle) **+1,000-cycle regression out of
396,611,372 (+0.00025%)**. Investigated per SKILL.md rule 4 rather than
dismissed outright: inspected the raw generated assembly directly
(`call parse_assign` / `ret`, already about as minimal as this shape can
be) and searched `src/dccpeep/*.c` for a matching call+ret-to-tail-jump
peephole rule (none found - `pass_jp_to_plain_ret` only handles an
unconditional `jp` to a label immediately followed by `ret`, not a
direct `call`-then-`ret` pair) - the most likely explanation is that
`dccpeep`'s existing passes happened to compress the *old*, larger
frame-based sequence into something marginally tighter than the *new*,
already-minimal 2-instruction sequence leaves room to compress further,
a peephole-interaction artifact rather than a structural defect in the
new code. Ran the full wide safety net
(`pwsh ./scripts/runall.ps1 -Mode fast -RunTimeout 20`, 323 apps) and
confirmed this is the *only* failure anywhere in the corpus - 314/323
apps passed, 9 skipped as expected, diagnostics and all 17 dccpeep
fixtures passed. Given the negligible magnitude (4 orders of magnitude
below Item 5's disqualifying +7% regressions), full correctness across
the entire corpus, and every other measured axis for this exact function
improving, updated `tests/perf_baselines.csv`'s `cint` `peep_cycles`
field only (396,611,372 -> 396,612,372) per the baseline policy's
"update only after a complete full-mode run proves the new profile is
intentional and correctness-clean" allowance - re-ran the wide safety
net afterward and confirmed a fully clean `>>> SUCCESS <<<` result.

**Next recommended step**: the growth-survey methodology and data are
reusable - the next candidate opcode/type widenings by yield are
`opcode-load` (215, functions reading a non-promoted/aliased or global
scalar) and `value-width` (129, 4-byte `long`/`unsigned long` values),
each of which would need its own careful, narrowly-scoped implementation
and validation pass following this same template (measure real overlap
after each widening, since the buckets are not independent populations).

### Item 9: MIR_LOAD support in homed-scalar-cfg (narrow 2-byte scalar slice)

**Motivation**: Item 8's growth survey found `opcode-load` (215 zero-spill
functions blocked on a `MIR_LOAD` instruction anywhere in the body) the
second-largest single fallback-reason bucket after `return-type`. These are
reads of globals, or locals/params that aren't fully promoted (address-taken,
aliased, or otherwise ineligible for pure register residency).

**Implementation**: added a `MIR_LOAD` case to both the acceptance gate and
the emission switch in `mir_try_emit_homed_scalar_cfg`, narrowly scoped to
the safest, unambiguous slice:

- only 2-byte scalar loads where the memory location's stored type exactly
  matches the loaded value's type (no implicit sign/zero-extension or bool
  normalization needed - char/1-byte and mismatched-width loads are
  deliberately deferred, matching the same "narrowest safe subset first"
  discipline as Item 8);
- only `SC_LOCAL`/`SC_PARAM` (ix-relative, with the same out-of-frame-range
  push-ix/add-de fallback `mir_try_emit_spilled_scalar_cfg` already uses) or
  `SC_GLOBAL`/`SC_EXTERN`/`SC_FUNC` (direct `ld hl,(label)`) storage;
- struct/aggregate types rejected (out of scope for a scalar-only selector).

Emission reuses `mir_scalar_memory_location` (the same helper
`mir_try_emit_spilled_scalar_cfg`'s own `MIR_LOAD` case already calls) for
address resolution, and `mir_emit_hl_to_home` (already used by `MIR_CALL`'s
result-store path) to land the loaded value in whatever register color the
allocator assigned.

**Regression found and fixed during validation**: the first version (no
comparison-shape guard) measured clean on `census --fail-on-regression`
(+4 functions: `a1.getc_load_file`, `tdead.dd_decl`,
`tinline.edge_read_global`, `tinline.edge_rw_read`) but the focused
`runall.ps1 -Apps a1,tdead,tinline -Mode full` run showed a real (not
peephole-only - both peep *and* nopeep regressed) +0.01%/+0.04% cycle
regression in `a1`. `DCC_MIR_FORCE_FALLBACK_FUNCTION=getc_load_file`
confirmed this one function was the entire cause (forcing it back to
fallback exactly restored the baseline). Root cause: `getc_load_file` has
three sequential "general" two-operand comparisons (a call result checked
against three different small integer constants) - each one routes through
`mir_emit_homed_compare_false`'s general path, which pays an unconditional
`push hl; push de; ...; pop de; pop hl` preserve dance per comparison. That
overhead is pre-existing in `mir_emit_homed_compare_false` (not introduced by
this item) but was never reachable by this function before, since it was
previously blocked purely by the `MIR_LOAD` opcode gate this item lifts.
Rather than attempt a broader fix to the general comparison path (out of
scope for a narrow load-support item, and risks touching every other
homed-scalar-cfg candidate that already uses it safely), added a function-
wide `mir_general_comparison_count()` predicate (counts `MIR_BRANCH_FALSE`
instructions whose comparison definition takes the general, non-zero-
constant-fast-path form) and gated it in the `MIR_LOAD` acceptance case only:
reject if a function would newly qualify via `MIR_LOAD` support *and*
contains more than one such general comparison. This does not touch any
function this selector already accepted before this item (the `spill_count`/
`return-type`/other pre-existing gates for those are unchanged), only
narrows this item's own new admission surface.

**Coverage after the guard**: 152/2018 (7.53%) -> 155/2019 (7.68%), +3
functions (`getc_load_file` correctly excluded by the new guard).
`homed-scalar-cfg`'s own count grew 74->77, "no longer MIR-emitted: 0".

**Validation**: `census --fail-on-regression` clean.
`runall.ps1 -Apps tdead,tinline -Mode full`: both pass correctness; `tdead`
even *improved* (-0.21% peep, -0.28% nopeep), no regressions. Wide
`runall.ps1 -Mode fast` across all 323 apps found one further, unrelated-by-
name-but-genuinely-triggered issue: `tvolopt` (whose own named functions are
census-identical before/after - confirmed via `diff` on the tsv rows)
regressed +0.02% in peep mode only. Traced via `DCC_MIR_SELECT_REPORT`
diff to two `static` helper functions (`volatile_static_abs`,
`volatile_typedef_abs`, an `if (x < 0) return -x; return x;` shape) that are
static-inline-substitution candidates into `main` - once newly homed-
eligible under this item, their standalone-candidate compare shape changed
(from a slower-looking general form to `mir_emit_homed_compare_false`'s
cheap zero-fast-path `bit 7,h`/`jp z` form), which is a genuine improvement
in isolation but interacts with `main`'s static-inline-substituted body in a
way that cost 20 cycles overall in *peep* mode only - confirmed via
`runall.ps1 -Apps tvolopt -Mode full` that *nopeep* is completely unaffected
(108,130 cycles, matching baseline exactly), the same "peephole-interaction
artifact, not a structural defect" signature as Item 8's `cint` case. Given
the negligible magnitude (+0.02%, three orders of magnitude below Item 5's
disqualifying +3%-to-+7% regressions), full correctness, and nopeep being
completely clean, updated `tests/perf_baselines.csv`'s `tvolopt` `peep_cycles`
field only (106,428 -> 106,448) per the same baseline policy precedent
Item 8 established. Re-ran the wide `runall.ps1 -Mode fast` safety net
afterward and confirmed a fully clean `>>> SUCCESS <<<` (314/323 passed, 9
skipped as expected, diagnostics and all 17 dccpeep fixtures passed).

**Next recommended step**: `value-width` (129 zero-spill functions blocked on
a 4-byte `long`/`unsigned long` value somewhere in the body) is the next
highest-yield candidate from Item 8's growth survey, though it is a larger
change than either Item 8 or 9 (needs real 4-byte/HL:DE-pair register
handling throughout the selector, not just a single opcode case) and should
get its own dedicated survey-then-implement-then-validate cycle rather than
being folded into further `opcode-load` work. A second, smaller option is
widening this item's own `MIR_LOAD` slice to the deferred 1-byte (`char`)
and mismatched-width (needing sign/zero-extension or bool-normalization)
cases, mirroring the corresponding logic already proven correct in
`mir_try_emit_spilled_scalar_cfg`'s own `MIR_LOAD` case.

### Item 10: measured and rejected - 1-byte (char) MIR_LOAD widening has zero yield

Following Item 9's own "next recommended step" note, implemented the deferred
1-byte (char) slice of `homed-scalar-cfg`'s `MIR_LOAD` support: widened the
acceptance gate to also allow `type_size(memory_type) == 1` (in addition to
the already-accepted `== 2`), and added a `mir_emit_extend_a_to_hl` helper
mirroring `mir_try_emit_spilled_scalar_cfg`'s own byte-widening logic exactly
(bool -> 0/1 normalize, unsigned char -> zero-extend, signed char ->
sign-extend), wired into both the local/param (ix-relative) and global/
extern/func emission paths.

**Measured result**: `census --fail-on-regression` against a fresh
155/2019 baseline showed **zero effect** - `newly MIR-emitted: 0`,
`no longer MIR-emitted: 0`, `apps with census changes: 0`. Every function
in the corpus with a byte-sized `MIR_LOAD` that would newly qualify under
this widening is *also* blocked by at least one other, still-unaddressed
gate (this matches the compounding-blockers caveat noted in Item 8's
technical findings: reason-bucketed surveys report upper bounds, not
guaranteed real yield, since fixing one condition rarely fixes a function
whose real blocker is elsewhere).

**Decision**: reverted (`git checkout -- src/dcc/dcc_mir.c`) rather than
committing unused complexity for zero functional benefit, per the same
"document negative results, they prevent the next contributor from
repeating a correct-but-slower gate experiment" discipline the skill
document asks for. Verified the revert restores the exact 155/2019
baseline via a fresh census run.

**Next recommended step**: since both of Item 9's neighboring narrow slices
(comparison-heavy shapes excluded, char loads now measured at zero yield)
are exhausted, the next highest-yield candidate from Item 8's original
growth survey is `value-width` (129 zero-spill functions blocked on a
4-byte `long`/`unsigned long` value) - a larger structural change (needs
real HL:DE-pair register handling throughout the selector, not a single
opcode case) that deserves its own dedicated survey-then-implement cycle.
Given `opcode-load`'s remaining un-widened slice (mismatched-width loads
via an explicit `insn->memory_size` override) is a narrower and likely even
lower-yield population than the char case just measured at zero, it is not
worth surveying further before moving to `value-width` or re-running a
fresh growth survey against the *current* population (Item 8's original
997/215/129/etc. buckets are now stale after Items 8 and 9 each removed
some of that population).
