# dcc MIR migration: accelerated roadmap to 60%

`mir-text-size-plan.md` is the authoritative experiment log. This file is the
current execution plan and handoff.

## Goal and current state

The immediate target is **1,216/2,026 ordinary functions (60.02%)** in
production MIR emission, without ordinary or stack-check removals and without
peep/nopeep regressions. Mixed-mode transactional fallback remains in place.
The long-term goal is 100% MIR-required coverage and removal of legacy
capture/replay only after the runnable and extended corpora pass.

- Branch: `perf/unified-regalloc`
- Published baseline: `45cf3f0`
- Published ordinary coverage: **890/2026 (43.93%)**
- Published stack-check coverage: **912/2128 (42.86%)**
- Current ordinary coverage: **909/2026 (44.87%)**
- Current stack-check coverage: **931/2128 (43.75%)**
- Latest production cohort: T425, cheap direct-home path for objectless
  single-use pointer parameters (+1/+1), resolving the T424 cost-model
  gap. Prior cohort: T405, call-result direct-reload narrow
  `storeind` (Stream B) - +2/+2 coverage, landed alongside T400
  (Stream D's MIR-only scalar address-escape filter, +3/+3), T403
  (Stream C's centralized named-address resolver + field-aware CSE,
  +1/+1), and T402/T406/T410/T411 (four zero-net architectural
  enablers: direct-reload wide storeind path, phi-slot spill/reload
  cleanup for large-gap backedges, planned store-address handoff
  extended across one same-block call, and an address-rematerialization
  retry that shrank `planned-index-base-cost` 38->19 ordinary). This
  "next 20%" wave's 4-stream parallel execution (foreground correctness
  stream + 3 background implementation agents, one integrator) found
  real wins early but has hit a strong, repeated dead-end pattern in
  its most recent rounds: `block-cse-cost` needs a selector-local MIR
  rollback or a real retained/rematerialized base planner (not another
  bounded VN extension - T407), `inline-substitution` needs TU-wide
  callee materialization or true MIR-native inlining, `phi-fallthrough-
  cost`, `wide-store-cost` (T408) and a fresh `unary-not-cost` re-rank
  (T412) all show no safe generalizable predicate, and Stream D's
  `text-size`/`indirect-store-address-cost`/`rhs-stack-cost` follow-up
  on its own T410 infrastructure found 0/12 clean in `rhs-stack-cost`.
  A full sweep of `mir-dead-ends.tsv` found **15 confirmed correctness
  bugs** logged project-wide under forced admission (all safely excluded
  by existing gates in production - none currently reachable). Root-cause
  investigation (T413) found and fixed a genuine miscompilation: T410's
  call-crossing planned-stack store-address path had a push/pop ordering
  bug in `dcc_mir_spilled_cfg.c` (`MIR_STORE_INDIRECT`'s call-crossing
  case popped the call-result value where the planned address should have
  been popped, and vice versa), corrupting memory whenever that exact
  shape was force-admitted. Fixed with a 4-line reorder; zero net
  coverage change (the path was unreachable in production - a latent
  risk, not a shipped bug). **Scenario-complete re-validation (T415)
  corrected T413's scope**: only **5 of the 7** originally-tested
  functions are genuinely fully fixed across every scenario each app
  declares (`bint.add_string`, `forint.add_stmt`, `tallocx.fill`,
  `too.bst_insert`, `attnc11.convert_weight_group`); the other 2
  (`adaint.var_or_const_decl`, `forint.run_prog`) only had their
  default-scenario symptom fixed - both still fail differently on their
  `ttt`/`sieve` extra scenarios, confirming a **second, separate,
  still-unfixed bug** shared with `tvapinit.join`/`tap.first_implementation`
  (a suspected hidden-phi-edge-use defect in forward-to-next/slot
  elision, per Stream B's independent lead - not yet root-caused).
  Status: **5/15 confirmed bugs fully resolved, at least 4/15 confirmed
  still open under one shared (not yet fixed) second mechanism, ~6/15 not
  yet re-tested against the T413 fix.** Active investigation continues in
  parallel with T414 (Stream C's `absolute-index-cost` rematerialization
  retry, 0 net coverage, real enabler) and continued mining of untried
  buckets (`dead-local-suffix-cost`, text-size re-bucketing).
- Earlier cohort: T396, signed wide-constant relational inline
  compare - ported legacy's `emit_signed_long_const_cmp_ast` exactly
  (sign-flip + biased 32-bit `sbc` sequence) as MIR's own inline codegen
  for a wide relational compare against a compile-time constant on the
  right operand, replacing the always-call-the-runtime-helper path for
  this shape. Found and fixed a real redundant-load pitfall in the
  caller (6 wasted bytes/call site) via a full-census diff before
  landing - the naive version passed all 5 forced-accept tests but
  regressed `tlongreg.test_compares` corpus-wide. +5 ordinary/+5
  stack-check, zero removals; focused full-mode validation on
  `tlong,tlongopt,tlongreg` clean (10 improvements incl. `tlongreg`
  peep -19.56% cycles); full extended gate clean (0 regressions, 408
  improvements). This closes the item T394 had explicitly scoped but
  deferred, and was `plan.md`'s top-ranked remaining architecture lead.
- Prior finding: T397, exhaustive `wide-constant-cost` re-testing after
  T396 shrank the bucket to 41 - found 12 more real clean wins (two
  large: `tpromo.test_integer_promotions` -8.99% bytes, `tlong.tshft`
  -10.13% bytes) but again **no safe generalizable threshold** separates
  them from 26 confirmed regressions at identical byte margins
  (`too.rect_perim`/`tctxops.sh_udiv` both margin +6, one regresses one
  wins). No code change; all 38 tested candidates recorded in
  `mir-dead-ends.tsv`. This bucket is now considered exhausted for
  gate-margin mining, matching T395's three buckets and T394's
  `unary-not-cost`.
- Latest production cohort: T394, unsigned wide-constant relational
  compares (`u_gtbig`/`u_lebig`-style) - legacy has no inline shortcut for
  unsigned wide relational compares against a constant either (it also
  calls `__ltu`/`__leu`/`__gtu`/`__geu`), so MIR's identical call-based
  codegen is call-for-call equivalent; the `wide-constant-cost` gate now
  admits this proven-safe shape on an instruction-count guard instead of
  requiring a strictly smaller byte count. +2 ordinary/+2 stack-check,
  zero removals, focused full-mode and full extended gates clean.
- Latest production cohort: T391, `branch-condition-cost`'s block-count arm
  narrowed from an unconditional `blocks > 2` to `blocks > 2 &&
  captured_instructions > 50` after full-mode A/B found the rejected
  multi-block population splits cleanly: `bint.compile_line` (27 legacy
  instructions) is a clean win, every other measured multi-block candidate
  (95-421 legacy instructions) regresses. +1 ordinary/+1 stack-check, zero
  removals, focused full-mode and full extended gates clean.
- T388 (prior cohort): `rematerialized-home-cost` calls==0 measured cohort
  (`mir_is_profiled_rematerialized_home_measured_cohort`, admits call-free
  single-block pointer/struct-member compound-assignment forms despite a
  positive raw instruction delta), +4 ordinary/+4 stack-check.
- **T389/T390/T391 exhaustively re-ranked 12 fallback buckets this segment
  and found the per-bucket near-miss vein is now sharply diminishing-return:**
  only T388 (+4) and T391 (+1) yielded real, safely-generalizable landable
  wins; every other bucket investigated (`dynamic-index-base-cost`,
  `block-cse-cost`, `absolute-address-cost`, `planned-stack-cost`,
  `lazy-parameter-cost`, `indirect-store-address-cost`,
  `indirect-store-stack-cost`) either confirmed more correctness bugs (10
  total found this segment, all currently harmless since gated off for
  unrelated reasons - see `mir-dead-ends.tsv`) or found real winners with
  **no safe generalizable predicate** (structurally identical candidates
  land on both sides of win/loss; call-count or instruction-count
  thresholds are frequently non-monotonic per-bucket). T390 additionally
  pinpointed a precise architectural lead: `absolute-address-cost`'s
  repeated `index*N` computation is not an indexaddr-CSE gap but a
  call-side-effect/aliasing analysis gap (repeated `loadind` of a global
  struct member cannot safely be reused across intervening opaque calls
  without proving the callee doesn't write back to it) - materially
  higher-risk/higher-effort than originally scoped. **Recommendation:**
  further material coverage gains now require either (a) the
  call-effect/aliasing analysis project just described, or (b) the
  `phi-fallthrough-cost` architecture fix (T384's phi-forwarding-across-
  labels lead, needed to unlock `tinline.edge_and`/`edge_conditional` and
  likely others in that 44-function bucket), rather than continued
  per-bucket near-miss mining.
- **T386 was reverted (see T387 in `mir-text-size-plan.md`).** A stale local
  `ntvcm` build undercounted `LD SP,HL` by 1 T-state all session, producing
  illusory double-digit "improvements" for MIR-heavy hot loops. CI caught a
  real (tiny) `forint (peep)` regression from T386 that every local
  measurement had missed; the local emulator has been rebuilt from
  `origin/main` and the offending commit reverted (`629df33`). The corrected-
  emulator full extended gate is clean at the reverted state (T384's
  numbers). Before trusting any cycle-count claim going forward, confirm the
  local `ntvcm` binary is current (`git fetch && git log HEAD..origin/main`
  should be empty) - CI always builds fresh and remains authoritative.
- New tooling: `scripts/mir-gate-margins.py`, a generic per-reason-bucket
  near-miss ranker consuming the existing census TSV (no per-gate formula
  duplication). Used to find the T384 near-misses; re-run after every
  architectural change to re-rank remaining populations.
- New tooling: `scripts/mir-mac-ngram-miner.py`, a generic n-gram miner for
  the two largest heterogeneous fallback populations (`text-size`,
  `boolean-phi-cost`). T385 used it to confirm the quick-win vein is mined
  out for now: `text-size`'s top idioms are generic calling-convention/
  prologue boilerplate (no single fixable pattern), and `boolean-phi-cost`'s
  real `ld hl,N/jp/ld hl,N/or l/jp z` idiom occurs in functions too large
  (33+ blocks) for MIR's own naive rendering to beat yet. Both automation
  tools are ready to re-run after the next architectural change.
- New tooling: `scripts/mir-forced-accept-batch.py`, a concurrent
  forced-accept full-mode A/B runner (one `runall.ps1` subprocess per
  `(app, function)` candidate, unique scratch build dir each, `ntvcm`
  freshness preflight) that turns what used to be N sequential manual
  A/B round trips into one batched command. `mir-dead-ends.tsv` is a
  checked-in ledger of confirmed non-wins (correctness bugs and perf
  regressions) with `(app, function, reason, delta, note, source)`
  columns; `mir-gate-margins.py --exclude-known mir-dead-ends.tsv` filters
  a fresh near-miss ranking against it so already-answered candidates are
  never re-investigated. T388 used both together to invalidate a
  pre-planned candidate list in minutes instead of one-at-a-time
  investigation, and to find its real win via a fast re-rank.

| milestone | ordinary target | gain from current |
| --- | ---: | ---: |
| 45% | 912 | +17 |
| 50% | 1,013 | +118 |
| 55% | 1,115 | +220 |
| 60% | 1,216 | +321 |

The ordinary whole-corpus census is the primary metric. Stack-check is a
mandatory secondary regression guard, not an alternate denominator.

## Immediate next steps

The branch is CI-green, currently at **902/2026 ordinary (44.52%)**, still
**+10 short of the first 45% milestone** and +314 short of the 60%
target. T389-T393 (prior segments) exhaustively re-ranked and forced-accept
A/B-tested every remaining fallback bucket's near-miss population (12+
buckets, 150+ candidates) and found the per-bucket static-metric mining
technique that produced T383-T388's gains is now **exhausted for most
buckets**: only small wins remain findable one at a time (T388 +4, T391 +1,
T394 +2), correctness bugs and structurally-inseparable win/loss pairs
dominate the rest. T396 (this segment) broke that pattern with a real
architecture fix (+5), confirming targeted codegen-architecture work is
now the higher-yield path relative to further gate-margin mining. T397/T398
(this segment) then confirmed that conclusion holds for every other bucket
checked too - gate-margin mining alone is exhausted project-wide, not just
for the buckets T389-T393 already covered.

**For the next session: do not restart per-bucket gate-margin mining, and
do not attempt the full `next50-slot-intervals` rewrite** (T399 found its
premise doesn't hold against the actual gate code - see the remaining-
leads list below). Both scoped architecture items now have weak-to-no
verified yield. The highest-value next step is continued direct
code/assembly inspection of real near-miss candidates (the technique that
found T399's phi-copy fix) looking for further concrete, bounded,
zero/low-risk emitter-quality fixes - prefer this over any large,
high-blast-radius rewrite until a fresh lead is independently verified
against the actual gate chain, not just assumed from campaign framing.

**T399 follow-on lead found but NOT yet attempted** (repeated-parameter-
reload asymmetry): direct MIR-dump inspection of `tbits.ti16_bits`
(`dead-local-suffix-cost`) found that a parameter assigned a "budget"
stable register home (e.g. `a` at `home=iy`, safe across the whole
function) is referenced directly at every use site with zero extra
instructions, while a second same-shape parameter that missed the
stable-home budget (`b`, `home=hl` - HL is needed for every intervening
operation, so its original param value is evicted almost immediately)
gets a **fresh `MIR_LOAD` instruction defining a brand-new SSA value at
every syntactic read** (`ti16_bits` re-loads `b` four separate times,
each a distinct value/slot) instead of being cached/reused across reads.
This looks like a real, reusable, likely broadly-applicable class - but
**do not implement generic same-block redundant-load CSE without a
materially different approach than the last attempt**:
`future-cse-address-pass`/T70 already tried general same-block
address/value CSE and it was falsified (lengthened live ranges,
increased fixed moves/backend slots, net coverage **loss**). Any new
attempt must explain concretely why it avoids T70's regression mechanism
(e.g. by restricting to provably call-free, branch-free spans, or by
improving the stable-home register budget itself - giving more
parameters a persistent register/IY-style home - rather than caching
repeated reloads of an HL-displaced value) before writing code, and must
be validated with the same train/holdout + forced-accept A/B discipline
as every other lead this session, not assumed correct from static counts
alone.

**T394 (this segment)** re-ranked `unary-not-cost`/`wide-constant-cost`
excluding known outliers per `plan100-reband-unary-wide-constant`.
`unary-not-cost` is confirmed fully mined out (smallest remaining shortfall
is 42 bytes once ranked by the gate's real byte-margin metric, not
instruction count). `wide-constant-cost` yielded one real, evidence-backed
fix: unsigned wide relational compares against a constant call the same
runtime helper in both legacy and MIR (legacy has no inline shortcut there,
unlike its signed sign-flip/subtract inline path), so the byte-size gate
was over-conservative for that exact shape; forced-accept A/B confirmed two
real wins (`tlongopt.u_gtbig`/`u_lebig`) with zero regressions. The signed
sub-case (`s_lt0`/`s_gt32767`/`s_ltm32768` boundary-value regressions vs.
`s_le100`/`s_gtm5` wins - all with identical instruction margins but
inconsistent byte margins) needs a real inline-codegen extension (matching
legacy's proven branch-fused sign-flip/`C+1`/`C-1` trick in the
value-materializing path too) and was scoped, not attempted, given its
small net yield (2 wins, 3 confirmed regressions) relative to the risk of
touching a widely shared codegen path. `ts.main`'s 606-byte-smaller
`wide-constant-cost` candidate was forced-accept tested directly and
**confirmed a real regression** (+0.12% peep cycles) - direct evidence the
existing `mir_has_format_runtime_call()` guard is correctly load-bearing,
not overly conservative.

**`campaign2-call-effect-analysis` is completed (T393)**: added
`mir_load_object_is_call_safe()` making `MIR_LOAD` CSE-eligible across
intervening calls for `static` globals proven (via the same whole-file
lexical scan `ast_for_hoist_global_member_value_supported` already trusts)
to never have their address taken and never be written anywhere in the
translation unit. Safe, zero regressions, clean full extended gate - but
**measured yield on the current corpus is zero**: the CSE retry this feeds
only fires for single-block functions with 3+ eliminations, and no current
candidate has 3+ reloads of a provably-safe global within one block
(`cobint.add_var` itself is 2 blocks). A follow-on experiment temporarily
widening the retry to <=3 blocks confirmed real candidates get smaller but
still fall short of their bucket's byte-margin gate (`absolute-address-cost`
needs a 6% reduction; `cobint.add_var`'s actual gap is ~4.6%) - reverted,
zero net promotions either way. The predicate is retained as real,
validated infrastructure for whenever multi-block CSE is revisited. The
**member-qualified case** (`Gst.var`-style struct-field reloads, the actual
`cobint.add_var` repro) remains unexplored: it needs a genuinely new
lexical-scan extension (tracking `base.field =`/`base->field =`/
`&base.field` patterns in `dcc_global_scan.c`, which today explicitly
excludes any dotted/arrow-qualified identifier from its write/addr-taken
tracking) - a real, but separately-scoped, follow-on with materially higher
soundness risk than the bare-global case just completed.

**T395 (this segment)** ran one more exhaustive fresh census re-ranking
across all 16 remaining buckets (`mir-gate-margins.py --exclude-known
mir-dead-ends.tsv`) specifically to check whether any instruction-based
near-miss also had a genuine byte-margin near-miss underneath it. First
closed `plan100-dead-local-suffix`: `tlngcond.main`'s dead locals are
already fully elided by the existing `mir_object_is_fully_promoted`
mechanism (the dead-store-elision hypothesis was wrong); its +42-byte gap
traces to deeper call-argument stack-spilling/frame-depth differences, a
separate and larger architecture topic, not a quick fix. Then forced-accept
tested every remaining candidate in three buckets whose existing threshold
excludes a plausible-looking population:
`dynamic-index-base-cost` (8 candidates below the proven 15-instruction
margin: 2 clean wins, 6 failures, no monotonic split by margin/blocks/
objects/promoted-loads), `absolute-address-cost` (6 `blocks==2` candidates
excluded by a T154-era restriction whose original regressor,
`cobint.emit_tok`, is now accepted through a different path: 3 clean wins,
3 failures, `memberaddr`-count looked promising until `emit_tok`'s own
count fell between the two populations), and `block-cse-cost` (4
`spilled-scalar-cfg` `blocks==1` candidates at the same margin already
proven safe for `homed-scalar-cfg`: 1 clean win, 3 failures including one
that regressed despite the single most favorable byte delta of the four).
**Conclusion: this pattern - a small number of real, individually-verified
wins with no generalizable predicate separating them from confirmed
regressions in the same bucket - now recurs independently across three
unrelated buckets in one pass**, on top of `wide-constant-cost`'s identical
finding in T394 and `planned-stack-cost`'s pre-existing `tc89fp.main`
entry. No code change; all 18 tested candidates (7 winners, 11 losers/
inconclusive) recorded in `mir-dead-ends.tsv` with full A/B evidence so
future sessions do not re-derive the same conclusions.

**T396 (landed after T395):** implemented item 1 below in full - MIR's own
inline signed wide-constant relational compare, a direct port of legacy's
`emit_signed_long_const_cmp_ast`. Found and fixed a real 6-byte/call-site
redundant-load regression (`tlongreg.test_compares`) via full census diff
before landing, not just forced-accept per-candidate testing. Result:
+5 ordinary/+5 stack-check, zero removals, full extended gate clean (0
regressions, 408 improvements), plus a large focused-cohort win
(`tlongreg` peep -19.56% cycles). See `mir-text-size-plan.md`'s T396 entry
for the full writeup. Coverage now: **902/2026 (44.52%) ordinary,
924/2128 (43.42%) stack-check**.

**T397/T398 (this segment, after T396):** exhaustively re-tested the
tightest-margin candidates across `wide-constant-cost` (T397, 38
candidates: 12 real wins, 26 losses/inconclusive) and
`absolute-index-cost`/`binary-load-pair-cost` (T398, 5 candidates: 3
wins, 2 losses) - the identical no-generalizable-threshold shape every
time (e.g. `too.rect_perim` and `tctxops.sh_udiv` sit at the exact same
+6 byte margin; one regresses, one is a clean win). No code change; all
43 candidates recorded in `mir-dead-ends.tsv`. **Gate-margin mining is
now considered exhausted across every remaining bucket this session
checked** - further coverage requires either genuine selector/emitter
quality work or a cost-model change, not more threshold tuning.
Coverage unchanged: 902/919 (see above).

**Continued per-bucket near-miss mining is no longer a viable path to 60%
on its own.** The remaining leads are architecture items, not gate nudges:

1. ~~**Signed wide-constant relational inline compare**~~ - **done, see
   T396 above.** (Originally scoped as T394's follow-on: extend MIR's
   value-materializing wide-comparison codegen to reuse the sign-flip +
   `C+1`/`C-1` inline shortcut instead of always calling `__lts`/`__les`/
   `__gts`/`__ges`. Landed exactly as scoped, including the boundary-value
   cases (0/32767/-32768) that had regressed under the old call-based
   codegen in T394 - the new inline path is a different, faster code
   shape entirely, so all 3 are now clean instead of needing separate
   characterization.)
2. **The `Gst.var` member-qualified extension** to `campaign2-call-effect-
   analysis` (above): extend `dcc_global_scan.c`'s lexical pre-pass to track
   member-qualified writes/address-taken (keyed conservatively by member
   name alone, ignoring the base expression's shape, to stay in the safe
   over-counting direction), then extend `mir_load_object_is_call_safe`'s
   sibling logic to `MIR_LOADIND`. Still gated by the same single-block CSE
   retry restriction, so likely needs the multi-block CSE question resolved
   too before it can show real yield. **Low expected value on current
   evidence**: the already-completed bare-global sibling (T393) measured
   **zero** net corpus promotions for the identical reason (no current
   candidate has 3+ eliminations within one block), so this extension is
   unlikely to yield anything until the block-count restriction itself is
   addressed (see item 4).
3. **`campaign-phi-fallthrough-architecture`** (was `mir60-boolean-control`'s
   phi-fallthrough lead, T384): of the 44 `phi-fallthrough-cost` functions,
   only ~8 sit within 10 instructions of the gate's margin (the rest are
   large CFGs with 100+ instruction gaps, mislabeled by the "last selector
   tried" reporting quirk, not real near-misses); all ~8 were already
   forced-accept tested in T202/T386/T387 and found to regress. This lead
   is exhausted, not just under-mined - do not revisit without a
   fundamentally different mechanism (real phi-forwarding that reduces
   instruction count broadly, not a threshold change).
4. ~~**`next50-slot-intervals`**~~ - **premise corrected/downgraded, see
   T399.** `mir_prepare_backend_slots` was already a standard
   interval-based linear-scan slot allocator with reuse (not the naive
   whole-value model the campaign framing assumed), and direct inspection
   of the three cited beneficiary gates (`block-cse-cost`,
   `wide-store-cost`, `planned-index-base-cost`) confirmed each is gated
   on its own specific, already-A/B-tested forwarding/handoff mechanism
   plus a measured margin - not on general slot-count/frame-size
   pressure. A full use-position interval-splitting rewrite is **not
   verified to unlock any of these three buckets** and carries very high
   blast-radius risk (touches all 1660 currently-accepted functions) for
   uncertain, indirect payoff. Do not attempt the full rewrite without a
   fresh, directly-verified connection between interval quality and a
   specific gate. T399 did land one real, zero-risk fix found during this
   investigation instead: `mir_emit_spilled_phi_copies`'s dead push/pop
   swap-safety machinery for provably-disjoint multi-copy phi groups
   (loop-header value handoffs) - see T399 for details. Both scoped
   architecture leads (this item and `Gst.var`, item 2 above) now have
   weak-to-no verified near-term yield; further architecture progress
   needs a fresh lead from continued direct code/assembly inspection.

Any of these is a multi-session engineering project requiring careful
design before the first line of code, not a same-session gate tweak. Follow
the commit cadence below for every change: focused cohort during
development, one fresh full-extended gate immediately before commit, push,
and wait for CI green before starting the next item.

**T413-T415 (this segment, current top priority): correctness-bug cluster
investigation.** A full sweep of `mir-dead-ends.tsv` found 15 confirmed
correctness bugs logged project-wide (real miscompiles under forced
admission, all safely excluded by existing gates - none currently
reachable in production). Cross-stream MIR-shape comparison found 7 of
them shared a motif (`address/indexaddr/memberaddr -> call <helper> ->
storeind`) implicating T410's call-crossing planned-stack store-address
path. Root-caused and fixed (T413, `b23e1cb`): a push/pop ordering bug in
`dcc_mir_spilled_cfg.c`'s `MIR_STORE_INDIRECT` call-crossing case popped
the call-result value where the planned store address should have been
popped and vice versa, corrupting memory whenever the shape was
force-admitted. Four-line reorder fix, zero net coverage (path was
unreachable in production). **Scenario-complete re-validation (T415,
`5e17047`) corrected T413's scope**: only 5 of the 7 originally-tested
functions are genuinely fully fixed across every scenario their app
declares; `adaint.var_or_const_decl` and `forint.run_prog` only had their
default-scenario symptom fixed - both still fail differently on their
`ttt`/`sieve` extra scenarios. This confirms a **second, separate,
still-open bug** shared with `tvapinit.join`/`tap.first_implementation`
(4 known repro cases now). Leading hypothesis (Stream B, not yet
confirmed/fixed): a hidden phi-edge-use defect in forward-to-next/slot
elision; a prototype fix regressed perf 7%+ and was not landed. Status:
5/15 bugs fully resolved, 4/15 confirmed still open under one shared
unfixed mechanism, ~6/15 not yet re-tested against T413 (in progress via
a dedicated sweep). Alongside this, T414 (`7541812`) landed a real,
zero-net-coverage `absolute-index-cost` rematerialization-retry enabler
from Stream C. See `mir-text-size-plan.md`'s T413/T414/T415 entries for
full evidence; do not assume the second bug is fixed until a session
reports a concrete root cause and a validated patch.

**T416/T418 (later same segment): the "second bug" split into two more
separately-fixed mechanisms, plus a third still open.** `tvapinit.join`
and `tap.first_implementation` (T416, `9a486d9`) turned out to be their
own pair of narrow bugs, not the same one as `adaint`/`forint`: wide
direct-next forwarding missed a backedge phi use (`mir_can_forward_hl_de_to_next`
now rejects any value with a phi use), and `mir_multiply_by_small_constant`
wrongly applied its 16-bit fast path to 32-bit multiplies (now restricted
to `<=2`-byte operands). Both confirmed fully fixed by direct repro;
independently cross-checked that this fix does **not** touch
`adaint.var_or_const_decl`/`forint.run_prog`'s residual bug. Separately,
T418 (`d7514b9`) found and fixed a real, different bug: `MIR_PHI`
destinations could have their slot store skipped by any of several
HL/DE forwarding fast paths in `mir_emit_virtual_store`/
`mir_emit_virtual_store_wide`, since none of them checked whether the
value being stored was itself a phi destination; fixed with a
`force_slot_store` guard. Fixes `tenumfsm.scan`; added a reusable forced-
MIR correctness regression harness (`scripts/mir-forced-correctness.ps1` +
`tests/mir_forced_correctness_cases.tsv`) so this and future fixes get
durable coverage. All zero-net-coverage. **Updated tally: 7 of 15
confirmed correctness bugs now fully fixed** (5 from T413, 2 from T416),
plus T418's `tenumfsm.scan` fix (not one of the original 15, found
independently) - **8 total now fixed**. `adaint.var_or_const_decl` and
`forint.run_prog`'s `ttt`/`sieve`-only failures remain a **distinct,
still-unidentified third mechanism** under active investigation - Stream
C found its root cause is `mir_emit_selfstore_incdec()` emitting an
illegal out-of-range IX-relative displacement (`inc (ix-196)`, outside
the valid signed-byte -128..127 range) for `adaint`; `forint.run_prog`'s
cause is still open (suspected `OP_DO` loop-bookkeeping issue, separate
from the displacement bug). See `mir-text-size-plan.md` T416/T418 entries
for full evidence.

**T419 (this segment, cluster nearly closed): the third mechanism is
fixed.** `mir_emit_selfstore_incdec()` emitted `inc (ix+d)`/`dec (ix+d)`
unconditionally, but Z80's IX-relative displacement is a signed 8-bit
value (-128..127); for out-of-range frame offsets (e.g. `adaint`'s large
frame, offset -196) the displacement byte wrapped and corrupted an
unrelated frame location. Fixed (T419, `9fedef5`) by factoring the
existing range check already used by `mir_emit_frame_word_store`/`load`
into a shared `mir_frame_word_uses_short_ix()` helper and reusing it in
`mir_emit_selfstore_incdec()`, falling back to
`frame_word_load -> inc/dec hl -> frame_word_store` when out of range.
Confirmed latent (0 hits in a full-corpus census before the fix); fixes
`adaint.var_or_const_decl` across all 3 scenarios; confirmed **not** to
touch `forint.run_prog`, which remains the sole unresolved bug from the
original cluster. Added to the forced-MIR correctness regression harness.
**Updated tally: 8 of 15 confirmed correctness bugs now fully fixed**
(5 from T413, 2 from T416, 1 from T419), plus T418's independently-found
`tenumfsm.scan` - **9 total fixed. `forint.run_prog` is now the only
open bug from this cluster**, under active investigation (suspected
`OP_DO` loop-bookkeeping issue). See `mir-text-size-plan.md`'s T419 entry
for full evidence.

**T420: the entire correctness-bug cluster is now FULLY CLOSED.**
`forint.run_prog`'s bug was a completely different mechanism than the
suspected `OP_DO` loop-bookkeeping issue: `run_prog` calls the
static-inline helper `set_sym_val()`. Legacy codegen normally substitutes
such helpers inline at the AST layer, and MIR lowering intentionally
skips marking their buffered bodies as needed, relying on the existing
`inline-substitution` fallback gate to keep such calls out of selected
MIR output - but that gate is bypassed under
`DCC_MIR_FORCE_ACCEPT_FUNCTION`, so the selected MIR caller kept a real
`call` to `set_sym_val` while its body was never emitted anywhere in the
file (unresolved call target). Fixed (`1033f2d`) with
`mir_mark_selected_inline_call_bodies_needed()`, which marks any
inline-substitutable callee's buffered body `deferred_body_needed=1`
once MIR output has actually won selection - reusing the existing
`MIR_CALL_FLAG_INLINE_SUBSTITUTABLE` flag, no new machinery. Zero net
coverage change (unreachable in production). **Final tally: 9 of the
original 15 logged correctness bugs are confirmed fully fixed** (5 from
T413, 2 from T416, 1 from T419, 1 from T420), plus T418's independently-
found `tenumfsm.scan` - **10 total fixed, and every bug in the original
investigation cluster is now closed.** The forced-MIR correctness
regression harness (`tests/mir_forced_correctness_cases.tsv`) now has 7
rows covering every fix in this cluster.

**T421: `cfg-backedge` (26 ordinary functions) investigated for a strata
loop-admission opportunity and confirmed closed for now.** With the
correctness cluster fully fixed, a fresh population breakdown found 9
structurally-safe candidates (single natural loop, no calls, single
backedge, not dispatch-loop-shaped) that are all forced-MIR
correctness-clean - but all 9 regress checked peep/nopeep cycles or size
under full-mode A/B. 14 more are risky (calls/multiple backedges/
dispatch-loop-shaped) and 3 are unknown. No safe, profitable admission
stratum exists; `cfg-backedge` remains blocked on profitability, not
correctness, and should not be revisited without a genuinely new cost
model.

**T422: `wide-constant-cost` (41) / `wide-store-cost` (36) re-mined after
the T416/T418 fixes, in case either unlocked new coverage. 25 forced
full-mode A/B checks across both buckets found only fresh
profitability-gated regressions - no missing selector, no reusable
predicate.** Both buckets are confirmed profitability-gated dead ends,
the same conclusion as `cfg-backedge`.

**End-of-wave status (superseded below)**: this "next 20%" wave's 3
parallel background streams (B/C/D) reached explicit stop conditions -
the correctness cluster is closed, and every architecturally-promising
bucket identified at the wave's start (`cfg-backedge`, `wide-constant-
cost`, `wide-store-cost`) is confirmed profitability-gated with no
reusable predicate. Coverage was unchanged since T405 at **908/2026
ordinary (44.82%)**, **930/2128 stack-check (43.70%)** - every fix this
wave was a genuine latent-bug repair with zero net coverage impact, not
a coverage-increasing change.

**T423: extended T393's bare-`static`-global call-safety CSE to the
member-qualified case (`Gst.var`-style fields), per the standing 100%-
coverage directive that architectural leads be resolved rather than
declared dead ends.** Reused T403's existing field-level call-safety
proof (`mir_resolve_isolated_global_field_load()` /
`mir_isolated_global_field_call_safe()`) inside a new
`mir_load_indirect_is_call_safe()` wrapper, threaded into
`mir_common_expressions_equal()`'s `MIR_LOAD_INDIRECT` case. This is
real, reusable infrastructure (not a name-based hack) - but the actual
candidate population (repeated `Gst.var`/`G->s_rs`/`G->words`-style
loads) is disqualified in every real instance found (the global is
written/address-taken elsewhere, or the pointer root isn't an isolated
global field), so this lands as a validated zero-risk generalization,
+0/+0 coverage.

**T424: audited `future-pointer-param-classifier` ("likely the single
largest remaining lever but highest risk" per the original backlog
note) and found the semantic problem it called for already solved in
production.** `mir_pointer_value_uses_are_eligible()` /
`mir_pointer_parameter_references_eligible()` /
`mir_filter_pointer_parameter_objects()` already walk every use of a
candidate pointer parameter and correctly distinguish safe dereference/
index/member/comparison/return uses from unsafe call-argument-forwarding
and address-of uses - exactly the distinction the original T55 whole-
symbol attempt could not make. Landed only exact `reason=call-argument`/
`reason=address-of` diagnostic tightening. Tested the more aggressive
reading of the backlog item - admitting every classifier-safe single-use
pointer parameter, not just the current narrower profitability
sub-filter (`uses > 1 || eligible_parameter_count > 1 || index/member
use`) - and this is a **real, measured regression**: ordinary
`908/2026 -> 906/2023` (+1 `tbool.set_bool`, -3 `tc99apar.
read_paren_const`/`tc99apar.read_paren_restrict`/`tdecl.pick_same_node`),
same +1/-3 pattern on stack-check. All three losses are trivial
single-use safe pointer shapes (bare `*p` / `return p`) that become
unprofitable once forced through the full object-promotion path. The
remaining blocker is therefore a **selector-quality/cost-model gap for
trivial single-use safe pointer shapes**, not classifier ambiguity - a
narrower, cheaper acceptance path bypassing full object promotion
(analogous to the earlier frameless-home-emission win) is the candidate
fix, and is under active follow-up investigation (see below) rather than
accepted as a final dead end, per the standing directive.

**T425: resolved T424's cost-model gap instead of accepting "blocked".**
Root-caused *why* the full object-promotion path was unprofitable for
`tc99apar.read_paren_const`/`read_paren_restrict`/`tdecl.pick_same_node`
but not for `tbool.set_bool`: the three losers were already zero-slot
direct parameter reloads under the existing narrower path, while
`tbool.set_bool` alone still spilled its pointer parameter to a backend
slot and reloaded it as the `storeind` address - full object promotion
was simply wider machinery than this single-use shape needed. Added a
narrow direct-home proof (in `dcc_mir_spilled_cfg.c`) for objectless
single-use pointer parameters whose sole use is one direct dereference/
address-formation/return, letting the spilled backend reload the
incoming `ix+N` parameter home at its one use site instead of
manufacturing a backend slot. This recovers `tbool.set_bool` with **zero
re-admitted losses** - the same "narrower, cheaper path bypassing
general-path overhead" pattern that produced the earlier frameless-home-
emission win.

**Coverage: 908/2026 -> 909/2026 ordinary (44.87%), 930/2128 -> 931/2128
stack-check (43.75%). This is the first real coverage-increasing change
in the wave since T405** - T413-T424 were all genuine correctness/
infrastructure work with zero net coverage impact; T425 demonstrates the
standing directive in practice: when gate-margin mining is exhausted and
a stream reports "blocked", root-causing the actual mechanism (not
accepting the report at face value) can still produce real, validated
coverage.

**T426: fresh gate-margin re-rank after T425 confirmed no further
threshold-mining opportunities exist, so a new architecture stream
implemented real kill-aware/allocator-aware block value numbering for
`block-cse-cost` (94 candidates, the largest unaddressed architectural
bucket) - a materially different mechanism than T70's already-falsified
naive same-block CSE.** The new VN pass tracked provable value identity
plus explicit kills (calls, indirect stores, aliased writes) and checked
the actual simulated register-allocation effect of each substitution
before admitting it. Real implementation, real measurement: the only
clean census admit (`cobint.add_stmt`, +1/+1) was found on closer
inspection to regress linked peep size (+0.46%) because the spilled CFG
backend emitted an 8-byte IX frame for the VN-shrunk MIR that `dccpeep`
could not remove - shrinking raw MIR instruction count does not
guarantee smaller final Z80 output when it forces a frame allocation the
peephole optimizer can't see through. A second concrete conflict was
found in `cobint.compile_stmt`: legacy block CSE (needed to preserve
existing admits) raises spills from 0 to 8, poisoning later VN
opportunities in the same function. **+0/+0 net coverage, but a genuine,
implemented, measured dead end** (not a speculative one) - the real
blocker for `block-cse-cost` is now understood to be a spilled-CFG-
backend/peephole limitation, not a missing CSE algorithm.

**Current status**: three architecture threads from this wave are now
closed with real evidence, not left as speculative dead ends: (1) the
T424/T425 pointer-parameter cost-model gap - resolved with a real +1/+1
fix, (2) `next50-slot-intervals` (confirmed not to hold per T399) and
`campaign2-call-effect-analysis`'s member-qualified case (resolved
zero-yield by T423, real infrastructure reuse), and (3) `block-cse-cost`
(T426 - implemented and measured, found a genuine spilled-CFG-backend/
peephole frame-elimination gap, not a CSE-algorithm gap). A fourth
stream is now investigating `phi-fallthrough-cost`'s confirmed-exhausted-
at-threshold-level bucket via real phi-forwarding-across-labels (never
previously attempted - only threshold nudges were tried and rejected in
T202/T386/T387). No stream has been allowed to settle on "blocked"
without either a concrete profitability proof or an active follow-up per
the user's explicit, repeated directive: the goal is 100% MIR coverage,
and gate-margin-exhausted buckets get a genuine architectural fix
attempt, not a declared dead end.

## Latest production cohort

The current cohort promotes three measured allocator-backed loop strata after
all specialized loop selectors decline: call-containing homed CFG, minimally
framed slotless spilled CFG, and bounded small-frame spilled CFG. Homed
selection publishes its actual frameless decision instead of relying on stale
spilled-selector slot state. The source-local-free leaf-loop invariant remains
load-bearing after a broader prototype re-enabled hidden `tstr.wcsrchr` and
regressed both runtime modes by more than 20%.

## Evidence and lessons

1. Narrow cost exceptions do not scale. After this in-flight cohort, do not
   land another narrow exception unless it unlocks at least ten ordinary
   functions or enables a larger architectural campaign.
2. Smaller raw streams are not proof of improvement. `ttype32.main` removed 59
   instructions but regressed peep cycles and linked size. Every promoted class
   needs affected-app full-mode measurement.
3. Broad profiling changes incumbent streams. Diagnostic controls accept exact
   comma-separated manifests or `*`; production admission remains structural
   and cannot contain app/function names.
4. Failed experiments are removed. The parallel PHI-copy scheduler produced
   zero coverage and has no residual production code.
5. Emitter improvements outrank gate widening. The dynamic-index wide-load fix
   turns two regressions into material wins and also admits a third function.
6. Candidate attempts are isolated. Every feature set starts from fresh MIR,
   feature state, output stream, and label base.
7. Static-body placement is part of correctness. A selector change can alter
   legacy inline decisions even when its Z80 is valid; selected hashes and
   census denominators must remain stable unless that change is deliberate.
8. Residual unused wide slots are not new headroom. The 319-function
   text-size census reproduces the previously rejected one-use wide
   binary-to-unary population.
9. Broad boolean-PHI work is no longer the next batch. All 158 residual
   candidates already simplify at least one valid PHI tree; only seven
   functions have extra-use blockers and seven have nontransparent constant
   edges, below the ten-function campaign threshold.

## Current impact ranking

The current ordinary fallback population is:

| priority | fallback reason | functions |
| --- | --- | ---: |
| 1 | text-size | 317 |
| 2 | boolean-phi-cost | 158 |
| 3 | dynamic-index-base-cost | 96 |
| 4 | block-cse-cost | 89 |
| 5 | unary-not-cost | 55 |
| 6 | wide-constant-cost | 48 |
| 7 | inline-substitution | 47 |
| 8 | phi-fallthrough-cost | 44 |
| 9 | planned-index-base-cost | 37 |
| 10 | wide-store-cost | 36 |
| 11 | absolute-index-cost | 30 |
| 12 | dead-local-suffix-cost | 29 |
| 13 | absolute-address-cost | 25 |
| 14 | cfg-backedge | 17 |

(As of T384's homed-scalar-cfg dead-store value elision: 890/2026 [+2],
912/2128 stack-check [+2], zero regressions. `dead-local-suffix-cost` fell
31->29 from the two admitted `tmirfast` functions; the two `absolute-index-
cost` near-misses from T383, `tptrlhs.touch_ptr_to_array_deref` and
`tc89init.main`, remain within four instructions of admission and are still
the next quick-win lead. `scripts/mir-gate-margins.py` is the tool to re-rank
this table after any future architectural change.)

Reasons are the last rejected candidate and overlap conceptually. Campaign
budgets therefore use net census gains, never sums of reason counts.

| campaign | planned net gain |
| --- | ---: |
| Boolean and acyclic control flow | +55 |
| Slots, addresses, CSE, and indexes | +115 |
| Calls, wide values, and systemic text size | +100 |
| Allocator-backed loops, inline substitution, and semantic tail | +58 |
| **Total** | **+328** |

If a campaign exceeds its budget, later risky work shrinks. If it misses, rerun
and re-rank the matrix immediately; do not compensate with function-name
exceptions or weaker performance standards.

## Risk policy

Accelerating the migration means tackling shared allocation, PHI, call, and
loop architecture earlier. It does not mean:

- accepting wrong code or hiding regressions in performance baselines;
- removing a semantic gate before its root cause is fixed;
- allowing a newly emitted function to regress in either runtime mode;
- adding app/function-name logic to production selection;
- combining transforms without independent feature controls and fresh streams.

Each high-risk campaign must be reversible through a feature mask, exact
affected-function manifest, census comparison, and focused runtime cohort.

## Campaign 1: boolean and acyclic control flow

Target cumulative coverage of at least 46%, then continue while this remains
the highest-yield matrix class. Planned remaining gain: **+55**.

1. Reclassify the 158 boolean-PHI, 55 unary-not, and 46 phi-fallthrough
   fallbacks by MIR shape and actual selected retry.
2. Add a MIR canonicalization pass that replaces boolean-value PHIs consumed
   only by a branch with predecessor-edge branches. Preserve PHIs with any
   value consumer.
3. Normalize unary `!`, double negation, and compare-to-zero before selection
   so all selectors consume one boolean representation.
4. Implement edge-aware parallel PHI copies only after the matrix identifies
   a real cyclic-copy population. Do not add frame homes merely to break a
   hypothetical cycle.
5. Strengthen the verifier for PHI predecessor completeness, arity, width, and
   edge dominance before enabling a cohort.
6. Use app-level train/holdout sets and promote only structural classes that
   pass both modes.

Stop after two coherent implementations if net gain remains below ten, and
move to Campaign 2 rather than tuning byte/block thresholds.

The first Batch 45 classification reached that stop condition: the residual
boolean-PHI blockers split into sub-ten-function classes, while the dominant
`non-phi` report consists of ordinary branch conditions rather than missed PHI
trees. Pause this campaign and execute Campaign 2. Return only when a fresh
matrix identifies a reusable ten-function boolean cause.

## Campaign 2: slots, addresses, CSE, and indexes

Target cumulative coverage of at least 52%. Planned gain: **+115**.

1. Start with the 101 `dynamic-index-base-cost` and 89 `block-cse-cost`
   candidates. Classify dynamic indexes by base lifetime, stride, calls,
   slots, and repeated use; classify CSE by eliminated opcode and residual
   frame/register cost.
2. Do not repeat the adjacent-DE dynamic-index handoff, which changed 83 app
   streams and promoted zero functions, or widen the single-block homed CSE
   five-instruction gate, whose rejected population includes measured peep
   regressors.
3. Replace whole-value backend-slot lifetimes with use-position intervals and
   safe splitting around calls.
4. Keep incoming parameters and rematerializable constants/addresses out of
   frame slots until a real clobber interval requires storage.
5. Coalesce representation-identical aliases, PHI copies, and one-definition
   forwards through one shared interference predicate.
6. Centralize symbol-plus-offset resolution across members and index chains.
   Loads, stores, address planning, CSE, and extern emission must use the same
   resolver.
7. Replace speculative block CSE with scoped value numbering that records
   alias class, kills, calls, and use count.
8. Build one index plan choosing retained, rematerialized, absolute, or stack
   bases from liveness and clobber data; retire parallel index heuristics.
9. Validate straight-line, acyclic CFG, call-containing, and aggregate-address
   cohorts independently before broad promotion.

## Campaign 3: calls, wide values, and systemic text size

Target cumulative coverage of at least 57%. Planned gain: **+100**.

1. Replace one-sequence nested-call staging with a call plan containing
   argument evaluation order, prepacked constants, nested results, cleanup
   bytes, specialized ABI eligibility, and clobbers.
2. Forward helper return registers directly to sole consumers when width and
   ABI already match.
3. Rematerialize wide constants and stable addresses by halves at their actual
   uses rather than assigning four-byte frame homes.
4. Select direct absolute and register-pair wide stores before bytewise generic
   indirect stores.
5. Re-bucket residual text-size functions by emitted assembly pattern and work
   only repeated patterns affecting at least ten ordinary functions.
6. Dynamically profile helper-heavy or loop-hot cohorts before promotion.

## Campaign 4: allocator-backed loops, inline substitution, and the semantic tail

Cross **1,216/2,026 (60.02%)**. Planned remaining gain: **+58**.

1. Fix backedge correctness through edge-aware liveness, PHI initialization,
   and loop-carried copies. Add forced-MIR focused coverage for every
   historically miscompiled shape first.
2. Admit loops in strata: one natural loop without calls, one loop with calls,
   then multiple backedges. Keep exact manifests and separate runtime cohorts.
3. Represent static inline substitution as a MIR-level call/inlining decision.
   Start with single-block scalar bodies, then acyclic bodies.
4. Use pointer-array and large-CFG tail work only if needed to cross 60%;
   these remain semantic implementations, never cost-gate bypasses.

Do not remove the legacy emitter at 60%. Continue the same measured process to
100%; removal requires MIR-required mode over runnable and extended corpora.

## Fast migration workflow

### Discovery

Run one ordinary and one stack-check candidate matrix per campaign with 24
compiler processes. Put `.mir`, `.mac`, TSV, logs, and reports under
`/dev/shm`. Rank structural signatures with DuckDB or parallel Python instead
of repeatedly recompiling the corpus.

### Development

1. Build the host compiler after each coherent edit.
2. Run the smallest affected app, then the entire affected-app cohort with
   `runall.ps1 -Mode full -FailFast`.
3. Compare ordinary and stack-check censuses only after a transform is
   functionally complete.
4. Run ASan/UBSan for allocator, CFG, ownership, or candidate-state changes.
5. Remove experiments that miscompile, regress either mode, gain fewer than ten
   ordinary functions after two coherent implementations, or require
   app-specific production logic.

Use approximately two-thirds of affected apps for development and reserve one
third as a holdout. Split by app, not function.

### Commit cadence

- Accumulate roughly ten coherent migration items per commit, or one
  indivisible high-risk architecture change.
- During development use focused full-mode cohorts and censuses; do not spend a
  full extended run on each item.
- There is exactly one `ntvcm` binary on this system, at
  `/home/dave/GitHub/ntvcm/ntvcm` (on `PATH`), built from the `main` branch of
  that repo. Before trusting any cycle-count claim (not pass/fail — see T387
  in `mir-text-size-plan.md`), confirm it is current:
  `git -C /home/dave/GitHub/ntvcm fetch && git -C /home/dave/GitHub/ntvcm log HEAD..origin/main --oneline`
  should print nothing; if it does not, `git -C /home/dave/GitHub/ntvcm pull`
  and rebuild with `./m.sh` before measuring. Do not create ad-hoc copies of
  the binary elsewhere (e.g. `/dev/shm/ntvcm-*`) — a stray stale copy is
  exactly what caused T387's illusory session-long "improvements".
- Immediately before every commit, run exactly one fresh:

  `TMPDIR=/dev/shm pwsh ./scripts/runall.ps1 -Mode full -Extended -RunTimeout 30`

- Do not use `-FailFast` for that gate. Failures-only output is already the
  default.
- Commit only the exact passing revision, include the coverage delta and exact
  promotions in the migration log, and push to
  `origin/perf/unified-regalloc`.
- Always wait for GitHub Actions (`gh run watch <id> --exit-status`) and
  confirm green before starting the next item. CI always builds `ntvcm` fresh
  from `davidly/ntvcm` and is the authoritative source of truth for any
  performance claim — see T387: it caught a real regression that every local
  measurement missed due to a stale local emulator.

## Best-practice constraints

- MIR transforms operate on MIR and metadata; selectors only select and emit.
- One helper owns each invariant: frame cost, address resolution, call
  planning, interval interference, PHI copies, and candidate lifecycle.
- Mutable feature state cannot survive an attempt.
- Semantic gates remain separate from profitability gates.
- Shared analyses have verifier assertions and focused tests.
- Diagnostics are opt-in and silent by default.
- Generated census and matrix files are never committed.
- Preserve unrelated `.vscode/settings.json`, `scripts/runall.ps1`, and
  `_crit/` changes.

## 60% completion criteria

1. Ordinary production MIR coverage is at least **1,216/2,026**.
2. Stack-check coverage has no removal from the current published baseline.
3. Every new or changed active function passes affected-app full mode.
4. The exact final revision passes the full extended pre-commit gate.
5. The milestone commit is pushed to `origin/perf/unified-regalloc`.
6. The migration log records coverage, exact promotions, rejected experiments,
   and the remaining population for the 60%-to-100% continuation.
