# dcc MIR migration - working plan (session handoff)

This file is a short, current-state pointer for picking the work back
up without re-reading every prior plan document. Full narrative detail
for the active vein lives in `mir-text-size-plan.md`'s Execution Log.
Older plan documents (`mir-migration-plan-to-100pct.md`,
`mir-migration-plan-100.md`, `mir-migration-plan-forward.md`,
`mir-migration-plan-next10.md`, `mir-migration-plan-next200.md`) are
retired history; do not resume numbering from them.

## Where we are

- Branch: `perf/unified-regalloc`.
- Coverage: 212/2022 runnable functions MIR-accepted (10.48%).
- `text-size` fallback is still the dominant reason (1,735/2021, ~86%
  of the corpus, ~97% of all fallback) - see SKILL.md's "Known root
  cause" section and `mir-text-size-plan.md` for the full analysis.
  Fresh re-bucketing (post-T9) still shows the gap population
  dominated by "far" (>256 bytes over legacy): near=1, close=31,
  mid=287, far=1,423; the far bucket's ratio signature (previously
  measured 66% at ≥1.6x generated/captured instruction-count ratio
  post-T5) matches SKILL.md's
  documented boolean-materialization/dead-store bloat signature in the
  general `mir_try_emit_spilled_scalar_cfg` selector - this remains the
  single biggest unaddressed lever (see `mir-text-size-plan.md`'s "Root
  causes to close" list carried over from this session's plan).
- Combined byte-sum reduction from this vein's Items T1-T9:
-1,542,587 bytes (~18.4%, T1-T4) plus Item T5's aggregate-return
  fix (which, uniquely among T1-T5, also moved coverage: +5 functions)
  plus Item T6's 3 landed struct-copy/assignment `ldir` fixes plus
  Item T8's jump-to-fallthrough elision (-2,582 bytes across 51
  functions, 1 genuine real-cycle win on `tdead`) plus Item T9's
  single-copy phi-merge push/pop elision (+1 newly-accepted function,
  `tvla.fixed_cast_bounds`; 170 apps with byte changes; 3 genuine
  cycle/size wins on `tvla`) plus Item T10's dead-store-feeding-value
  slot-allocation elision + dccpeep `local_alloc_wide` peephole (+1
  newly-accepted function, `tgoto.gt_block_label`; 8 genuine cycle/size
  wins on `tbug2`/`tdmfuse`/`tgoto`/`tmirslot`/`tvla`; a real, general
  dccpeep peephole gap closed for 3-/4-byte stack-only frames) plus
  Item T11's generalization of the div/mod-only constant-RHS-to-DE
  materialization (Item 16) to every binary operator (+5
  newly-accepted functions, the biggest single-item coverage jump
  since T5, crossing 10% coverage for the first time; 254 apps with
  byte changes; 10 genuine cycle/size wins across the 8 focused apps)
  plus Item T12's `mir_value_only_used_by_dead_unary` fix (a value
  whose only use is a dead `MIR_UNARY`, e.g. the `(void)param;`
  cast-to-void idiom, no longer forces its own load; +2 newly-accepted
  functions, 14 apps with byte changes, 2 genuine tiny cycle wins)
  plus Item T13's relaxation of `mir_can_forward_hl_to_next`'s
  `MIR_RETURN`-forwarding gate (dropped the overly-broad
  `mir_function_has_any_call()` restriction, keeping only the genuine
  `has_vla` hazard; resolves the previously-deferred Item T12b) plus a
  pre-existing `MIR_INDEX_ADDRESS` constant-multiply-to-`__mulu` defect
  it exposed and this item also fixed (+8 newly-accepted functions,
  253 apps with byte changes - the largest blast radius of the
  session - 9 genuine cycle/size wins across the 8 focused apps)
  plus Item T14's shared-epilogue fix (a function with 2+ `MIR_RETURN`s
  no longer duplicates the full ix/iy/sp-restore epilogue at every
  early return - a `jp` to one shared copy now, mirroring legacy;
  0 coverage change, -7,188 bytes across 310 functions)
  (0
  coverage change for T6/T8, byte
  reduction across the still-fallback
  population) across the whole corpus vs. the pre-T1 baseline, with 0
  real regressions (only digit-width text-metric artifacts from T2/T3,
  each independently confirmed non-real via a label/offset-normalized
  diff; T4/T5/T6/T8/T9/T10/T11/T12/T13/T14 had 0 unaccepted regressions - T10's tiny
  `cobint`/`tgoto` residuals were traced in full and match SKILL.md's
  documented "code-placement sensitivity" noise class). Item T6 also found and
  **deferred** a 2-site fix (`MIR_CALL`/`MIR_CALL_AGGREGATE`
  struct-argument-copy) that is provably beneficial in isolation but
  was withheld because it exposes a pre-existing, unrelated Root-Cause-C
  redundant-address-recomputation bug in the one function it currently
  affects (`tsretret.make_normal`) - see `mir-text-size-plan.md`'s
  Item T6 entry for the full stash-based A/B proof. Item T7 (comparison-
  fusion) was investigated and **deferred without shipping code**: the
  fusion angle SKILL.md originally flagged is already fully implemented
  from an earlier phase; the real remaining lever (call-result
  HL-forwarding) is a previously-identified occupancy-safety hazard
  (see `mir-migration-plan-100.md` Items 14/16) that this session's own
  narrow experiment confirmed produces 0 measurable win without
  stacking a third, independently-risky change - see `mir-text-size-
  plan.md`'s Item T7 entry for the full rationale.
- **`src/dcc/dcc_mir.c` has been split into 6 files** (this session):
  `dcc_mir.c` (core: lowering, capture API, CFG/dataflow analysis,
  register allocation), `dcc_mir_emit_common.c` (shared scalar-value
  emission helpers + the DAG selectors), `dcc_mir_homed_cfg.c`
  (`mir_try_emit_homed_scalar_cfg`), `dcc_mir_spilled_cfg.c` (the
  dominant `mir_try_emit_spilled_scalar_cfg` selector + its exclusive
  helpers), `dcc_mir_select.c` (loop selectors, dispatcher,
  `mir_end_function`), and a new internal header `dcc_mir_internal.h`
  (shared IR types/state + cross-file helper prototypes - not part of
  the public `dcc_mir.h` API). This was pure code motion: verified via
  a byte-identical full census (`--fail-on-regression` reported 0
  newly/no-longer-emitted functions and 0 apps with any census-metric
  change), a clean rebuild with no new warnings, and full `-Mode fast`
  + `-Mode full` safety nets (323 apps, all passed). `build-dcc.sh`
  needed no changes (it already globs `./*.c`).
- All correctness and performance validation is clean: full 323-app
  `-Mode full` safety net passes, 0 regressions.

## Recently landed (this session)

- **Item T2**: removed a dead 32-bit-operand exclusion from
  `mir_binary_is_fusable_comparison`, added
  `mir_emit_fused_wide_comparison_branch` to fuse wide
  compare+conditional-branch directly instead of materializing and
  reloading an explicit boolean. -16,643 bytes, 0 regressions.
  Committed `5d494c8`.
- **Item T3**: root-caused and fixed a dead `mir_virtual_iy_base` gate
  in `mir_can_forward_hl_to_next` (left over from before Item T1 gave
  that flag a real, non-constant value), unlocking general single-use
  HL-forwarding for small-frame functions. Required lowering Item T1's
  `frame_bytes` enable threshold from 150 to 140 to resolve a
  threshold-interaction regression this uncovered
  (`tptrlhs::touch_ptr_to_array_deref`). Committed `a7605b7`.
- **`dcc_mir.c` file split** (this session, see above). Committed in
  this same session.
- **Item T4**: removed the analogous dead `mir_virtual_iy_base` gate
  from `mir_can_forward_stack_to_index` (same root cause and fix shape
  as Item T3, found via the same `git log -S` methodology). -71,913
  bytes across 135 apps, 0 regressions, coverage unchanged (still-
  rejected candidates got smaller without crossing acceptance).
- **Item T5**: found and fixed `MIR_RETURN`'s struct-object case fully
  unrolling a byte-by-byte aggregate copy instead of using the Z80
  `ldir` block-copy instruction (legacy's equivalent path uses a
  `djnz` runtime loop, never unrolled). Replaced with `ldir`. **+5
  newly-accepted functions** (190 -> 195, 9.40% -> 9.65%) - the first
  Item since T1 to move coverage, not just shrink still-rejected
  candidates. 0 regressions; focused full-mode run showed 6 genuine
  performance improvements (real cycle-count/`.COM`-size wins),
  baselines updated for the 3 affected apps only.
- **Item T6**: surveyed 5 more struct-copy/assignment sites sharing
  T5's unrolled-copy defect (`MIR_STORE`, `MIR_COPY_AGGREGATE` in both
  `dcc_mir_spilled_cfg.c`/`dcc_mir_homed_cfg.c`, and
  `MIR_CALL`/`MIR_CALL_AGGREGATE` struct-argument-copy). Landed the
  `ldir` fix on the first 3; **deferred** the last 2 after a full
  `runall.ps1 -Mode full` A/B proved they flip `tsretret.make_normal`
  to acceptance with a genuine (if tiny) cycle-count regression -
  root-caused via a stash-based before/after test to a pre-existing,
  unrelated Root-Cause-C-class redundant-address-recomputation bug the
  byte reduction merely exposed, not something T6 introduced. 0
  coverage change (195/2021, 9.65%), 0 regressions in the final
  (post-revert) validation.
- **Item T7** (deferred, no code change): investigated `check_s`'s
  34-byte gap directly. Found the compare-fusion work this plan
  originally flagged as the single biggest lever (Items 1/2/4/25/27)
  was **already landed** in an earlier migration phase - `check_s`'s
  boolean is already fully elided. The real remaining gap is a call
  result (`v = call strcmp`) still being stored/reloaded because
  `mir_can_forward_hl_to_next` unconditionally excludes `MIR_CALL`-
  defined values, plus a separate `MIR_CONST` (the compare's `0` RHS)
  sitting in between that the forwarding skip-logic doesn't know is
  silent. Confirmed this exact idea was already investigated and
  deferred twice in `mir-migration-plan-100.md` (Items 14 and 16), for
  a documented "occupancy-safety" hazard (dynamic emission-order state
  a static accounting pass can't safely prove). This session's own
  narrow attempt (dropping just the `MIR_CALL` exclusion, plus a
  silent-`MIR_CONST` skip) produced 0 measurable coverage/byte win in
  isolation - it only helps once a third, independently-risky change
  (loosening an equality-check gate that may also make Item 15's
  label-skip capability dead-on-arrival) is *also* made. Reverted;
  documented in `mir-text-size-plan.md`'s Item T7 entry as a defer,
  same rationale class as Item 6/14/16 - a real opportunity exists but
  needs its own dedicated occupancy-safety design work, not a
  stacked-on-top follow-on edit.
- **Item T12**: added `mir_value_only_used_by_dead_unary` (mirroring
  Item T10's `mir_value_only_used_by_dead_stores`): a value whose only
  use is as the operand of a `MIR_UNARY` (cast, +, -, ~, !) whose own
  result has no use - the common `(void)param;` idiom in
  callback/visitor signatures - no longer forces its own load. Wired
  into `mir_prepare_backend_slots`'s slot-skip chain plus
  `MIR_LOAD`/`MIR_CONST`'s dead-value skip checks in
  `dcc_mir_spilled_cfg.c`, and the matching one-line guard added to
  `dcc_mir_homed_cfg.c`'s `MIR_UNARY` case. **+2 newly-accepted
  functions** (`tdecl.pick_same_node`, `too.scale_all_visitor`;
  202/2021 10.00% -> 204/2022 10.09%), 0 regressions, 2 genuine tiny
  cycle wins. Also investigated (but **deferred**, see "Item T12b" in
  `mir-text-size-plan.md`) a second, harder finding from the same
  `bint::goto_line_op` investigation: `mir_can_forward_hl_to_next`'s
  adjacency gate makes its own NOP/label-skip capability (Item 15)
  dead-on-arrival for every consumer except `MIR_RETURN` - confirmed
  via `git log -S`/`git show` on `fed34c9` this is original, deliberate
  design (not a stale flag like Items T1/T3's fixes), so relaxing it
  needs the same occupancy-safety design work already flagged as an
  open risk for Item T7 - left fully scoped for a future session.
- **Item T13** (resolves the deferred "Item T12b" above): removed the
  `mir_function_has_any_call()` half of `mir_can_forward_hl_to_next`'s
  `MIR_RETURN`-forwarding gate, keeping only the genuine `has_vla`
  hazard (a whole-function call-presence check had no adjacency link
  to the specific value being forwarded - static reading of
  `mir_emit_virtual_iy_epilogue`'s `exx` trick found no tied hazard,
  and full validation found none either). This alone flipped
  `t2denum.main` to accepted with a genuine nopeep regression
  (+3.69%/+2.44% bytes); root-caused via
  `DCC_MIR_FORCE_ACCEPT_FUNCTION=main` **on the pre-T13 tree** as a
  wholly separate, pre-existing defect merely exposed by crossing the
  acceptance threshold (Item T6's precedent class): `MIR_INDEX_ADDRESS`'s
  dynamic-index stride multiply never routed through the existing
  `mir_mul_const_fast_path_eligible`/`mir_emit_mul_hl_const` shift/add
  fast path, always calling `__mulu` even for compile-time-constant
  power-of-2 strides. Fixed both `MIR_INDEX_ADDRESS` call sites to use
  the fast path when eligible. **+8 newly-accepted functions**
  (`t2denum.main`, `tautolcs.main`, `tenumfsm.main`, `texlog.main`,
  `tmirslot.forward_into_store`, `trw.fail`, `tsretmem.hi_in_return`,
  `wumpus.prmt`; 204/2022 10.09% -> 212/2022 10.48%, the single largest
  coverage jump and blast radius (253 apps) this session), 0
  regressions, 9 genuine tiny-to-small performance wins (largest:
  `texlog` peep -0.68% cycles/-1.89% bytes). Milestone-tier full safety
  net (323 apps) run given the blast radius; clean.
- **Item T14**: `wumpus::pact` was 2 bytes over threshold; traced to
  both MIR selectors duplicating the full ix/iy/sp-restore epilogue at
  *every* `MIR_RETURN`, instead of emitting it once and having early
  returns `jp` to it like legacy already does. Fixed in both
  `dcc_mir_spilled_cfg.c` and `dcc_mir_homed_cfg.c`: only the "owner"
  return (the function's true tail) keeps the inline epilogue; every
  other return jumps to a lazily-allocated shared label. 0 coverage
  change (`pact` itself crossed the `text-size` threshold but remains
  separately blocked by the pre-existing `cfg-backedge` boundary - its
  own loop), 0 regressions, **-7,188 bytes across 310 functions**
  corpus-wide. 1 app (`tc89size`) needed runtime validation: PASS,
  +0.01%/+0.01% (10-11 cycles, the expected `jp`-overhead trade-off).
  Milestone-tier full safety net (323 apps) run given the 131-app
  blast radius; clean.

## Next session should

1. Execute the dedicated `text-size` plan (drafted this session,
   carried into `mir-text-size-plan.md`'s Execution Log after each
   item lands):
   - **Comparison-fusion is already done** (Items 1/2/4/25/27, landed
     in an earlier migration phase) - do not re-attempt it; Item T7
     confirmed `check_s`'s boolean is already fully elided.
   - **Re-sweep the worst-ratio/bucket list fresh post-T14** before
     picking the next candidate - 310 functions changed byte counts
     since T13, so a stale sweep from before T14 risks misdirecting
     the next pick.
   - **`wumpus::pact` is now blocked solely by the `cfg-backedge`
     migration boundary** (a deliberate barrier, not a bug) - worth
     revisiting as its own future item once enough non-loop
     `text-size` candidates are exhausted.
   - **Item T7 (call-result HL-forwarding) should be revisited now**:
     `mir_can_forward_hl_to_next`'s gate is now better understood and
     partially relaxed (Item T13) - re-examine whether the remaining
     occupancy-safety concern Item T7 deferred on still fully applies,
     or whether T13's finding (the `MIR_RETURN`-only carve-out's
     `has_any_call` half was simply overcautious, not load-bearing)
     changes the risk calculus for call-result forwarding too.
   - The single biggest remaining lever is now the **call-result
     HL-forwarding gap** (Item T7, deferred): `mir_can_forward_hl_to_next`
     unconditionally excludes `MIR_CALL`-defined values from the
     store/reload-elision path, even for a single, immediately-
     following use. This is deferred pending a **whole-function
     occupancy-safety pass** (design work Item 14 already called for,
     reaffirmed by Item 16 and this session's Item T7) - do not retry
     the narrow "just drop the exclusion" edit without that design in
     place first; this session confirmed it produces 0 measurable win
     alone and needs at least one more independently-risky change
     (loosening `mir_forward_skip_target`'s equality-check gate,
     which may also make Item 15's label-skip dead) stacked on top.
     If tackled, scope it as its own multi-item project, starting with
     verifying whether Item 15's label-skip capability is actually
     reachable today for any real function (this session's evidence
     suggests it may not be) before touching call-result forwarding.
   - Re-evaluate a complementary dead-store-elimination dccpeep pass
     (SKILL.md notes dccpeep's existing same-basic-block pass only
     removes one of two store/reload round-trips from this pattern,
     leaving a second, genuinely dead store) as a lower-risk
     alternative/complement that doesn't require the occupancy-safety
     proof.
   - Continue Root Cause C's residual (non-adjacent single-use
     forwarding) - this shares the same occupancy-safety design need
     as Item T7's call-result forwarding, so consider designing them
     together. Fixing Root Cause C's redundant-address-recomputation
     bug would also unblock re-applying Item T6's deferred
     `MIR_CALL`/`MIR_CALL_AGGREGATE` struct-argument-copy `ldir` fix
     (see `mir-text-size-plan.md`'s Item T6 entry) as a direct bonus -
     check that first when starting Root Cause C work.
   - **Item T8 landed** (jump-to-fallthrough elision in the `MIR_JUMP`
     case of both `dcc_mir_spilled_cfg.c` and `dcc_mir_homed_cfg.c`):
     0 coverage change, -2,582 bytes across 51 still-fallback
     candidates, 1 genuine real-cycle win (`tdead`, baseline updated).
     `gt_block_label`'s gap narrowed from 14 to 5 bytes but didn't
     flip - it also has a second, separate dead-store bug (a store
     immediately overwritten across the `goto`) that does NOT appear
     to be covered by the existing Item 16/17 dead-store-elimination
     infrastructure; worth investigating why as its own item before
     assuming that infra is complete.
   - **Item T9 landed** (`mir_emit_spilled_phi_copies` no longer
     routes a single-copy phi merge through a needless push/pop stack
     round-trip - direct load-then-store when `copy_count == 1`, since
     the swap-safety concern the general push-all/pop-all-reverse
     shape exists for is moot with only one copy pending): **+1
     newly-accepted function** (`tvla.fixed_cast_bounds`, 195->196,
     9.65%->9.70%), 0 regressions, 170 apps with byte changes, 3
     genuine cycle/size wins on `tvla` (baseline updated).
     `inline_fold_check` (the near-miss that surfaced this bug, 2-byte
     gap) shrank further but is now blocked by `inline-substitution`
     instead of `text-size` - worth a direct look as a possible next
     near-miss once its actual blocker is understood.
   - **Item T10 landed** (dead-store-feeding-value elision extended
     from emission time to `mir_prepare_backend_slots`'s slot
     allocation, `dcc_mir_spilled_cfg.c`, plus a general dccpeep
     peephole widening, `pass_local_alloc_wide` in
     `peep_pass_final.c`, compacting 3-/4-byte stack-only frame
     allocations to N x `dec sp` the same way the existing
     `try_local_alloc_at` already did for 1-/2-byte frames): **+1
     newly-accepted function** (`tgoto.gt_block_label`, 196->197,
     9.70%->9.75%), 0 unaccepted regressions, 8 genuine cycle/size
     wins across `tbug2`/`tdmfuse`/`tgoto`/`tmirslot`/`tvla` (baselines
     updated), plus a genuinely tiny (+0.047%) `cobint` residual
     traced in full to SKILL.md's documented "code-placement
     sensitivity in interpreter heaps" class (baseline updated after
     exhaustive tracing showed the underlying `.mac` diff is a clean,
     objectively-cheaper conversion with no other differences).
     **Important discovered discipline**: widening an existing
     fixed-point-internal dccpeep pass in place can permanently and
     irreversibly consume text that a later-converging,
     precondition-dependent pass in the same `fixed_point_passes[]`
     array (e.g. `tests/ttt.c`'s `_MinMax`-specific frame-shrink
     passes) still needs on a later iteration - caused and then fixed
     a real +1.08% regression on `ttt` this item. Any future
     peephole widening must check for this hazard first and, if
     present, add the widening as a new post-convergence
     `RUN_PASS(...)` call (after the fixed-point `do-while` loop
     exits) rather than editing the existing pass in place.
   - **Item T11 landed** (generalized the div/mod-only constant-RHS-
     to-DE materialization, Item 16, to *every* binary operator in
     `dcc_mir_spilled_cfg.c`'s `MIR_BINARY` case - a constant load can
     never clobber HL regardless of operator, so Item 16's own
     restriction to `/`/`%` was an artificial scope limit, not a real
     safety requirement): **+5 newly-accepted functions**
     (`tc99scpe.mid_block_simple`, `tinlinfb.local_helper`,
     `tpostptr.bump_local_paren`, `tunused.aggregates`,
     `tunused.scalars`; 197->202, 9.75%->**10.00%**, the biggest
     single-item coverage jump since T5 and the first time this effort
     crossed 10%), 0 unaccepted regressions, 254 apps with byte
     changes (broad blast radius, as expected for the dominant
     arithmetic path), 10 genuine cycle/size wins across the 8 focused
     apps (baselines updated); 3 tiny peep-mode deltas
     (`tunused`/`tinlinfb`/`tpostptr`, 0.01%-0.07%) fully traced via
     `.mac` diff to the expected legacy-vs-MIR code-shape noise of a
     function flipping from fallback to accepted (not a regression in
     previously-accepted output) - accepted, same noise category as
     T5/T9's own newly-accepted functions. Milestone-tier full
     `-Mode full` run (323 apps) passed cleanly given the broad blast
     radius. The pre-existing `!mir.has_vla` restriction (from Item 16)
     was kept, so `tvla`'s own `vla_sizeof_op_*` near-miss family is
     still NOT fixed by this item - a narrower, VLA-safe variant of the
     same idea is a plausible future item now that the general case is
     proven safe.
   - Re-run the full census and re-bucket the `text-size` gap fresh
     before picking each next item; the population shifts as items
     land (this session already caught one stale-ranking trap this
     way - the prior top outlier dropped out of the list entirely
     after Item T5 landed, Item T8 shifted 51 functions' byte counts,
     Item T9 shifted 170 more, Item T10 shifted the population again
     via both the slot-allocation and peephole changes, and Item T11
     shifted 254 apps' byte counts - re-derive the near-miss ranking
     fresh rather than reusing any list from before T11).
   - **Item T12 landed** (`mir_value_only_used_by_dead_unary`, see
     "Recently landed" above): **+2 newly-accepted functions**
     (202/2021 10.00% -> 204/2022 10.09%), 0 regressions, 14 apps with
     byte changes, milestone-tier full run clean given the core-path
     touch. **Item T12b deferred** (same entry): relaxing
     `mir_can_forward_hl_to_next`'s `MIR_RETURN`-only gap-forwarding
     gate to help non-return consumers - confirmed via `git log -S`
     this is deliberate original design, not a stale flag, so treat as
     a genuinely new capability requiring the same occupancy-safety
     design work already flagged for Item T7 before attempting; do not
     retry the narrow "just relax the gate" edit alone.
   - Continue down the post-T11/T12 near-miss bucket list toward the
     next non-VLA, non-T7/T12b-blocked candidate: `tenumfsm::main`,
     `pint::while_stmt`, `tstructv::assign_return_pair_ptr`,
     `tdmfuse::sdm_pair`/`sdm_pair_r`, `tesc::check_s`/
     `tscanf::check_str`/`tstr3::check_s`/`tsyntax::check_s` are the
     next-ranked candidates from the last bucket sweep - re-derive a
     fresh sweep first since T12 changed 14 apps' byte counts.
2. Now that the module is split, prefer editing the specific
   `dcc_mir_*.c` file that owns the relevant selector/helper rather
   than re-growing `dcc_mir.c` itself; add new cross-file prototypes to
   `dcc_mir_internal.h` (not the public `dcc_mir.h`) if a new helper
   needs to be shared across the split.

## Non-negotiable process reminders (see SKILL.md for full text)

- Snapshot the census **before** editing every single time.
- Every item needs: build clean -> regression-gated census
  (`--fail-on-regression`) -> focused `-Mode full` on the exact
  census-reported affected-app list -> a wide `-Mode fast` (or, for a
  structural/non-functional change like a file split, full `-Mode full`)
  safety net across the full corpus -> Execution Log entry -> commit ->
  push to `origin/perf/unified-regalloc`.
- Never accept a perf baseline movement without confirming, per app,
  that it is explained by a proven-necessary correctness fix (or is
  genuine noise below ~0.1% offset by an equal-or-larger improvement in
  the same app) - never to "hide" a real regression.
- Clean up scratch census files (`build/mir-*.tsv`) and any `/tmp/`
  debugging artifacts before committing; they are not tracked and
  should not accumulate in `build/`.
