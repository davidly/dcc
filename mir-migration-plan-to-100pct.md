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
