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
- Coverage: 195/2021 runnable functions MIR-accepted (9.65%).
- `text-size` fallback is still the dominant reason (1,744/2021, ~86%
  of the corpus, ~97% of all fallback) - see SKILL.md's "Known root
  cause" section and `mir-text-size-plan.md` for the full analysis.
  Fresh re-bucketing (post-T5) still shows the gap population
  dominated by "far" (>256 bytes over legacy): near=2, close=31,
  mid=282, far=1,429; 941/1,429 (66%) of the far bucket shows a
  ≥1.6x generated/captured instruction-count ratio, matching SKILL.md's
  documented boolean-materialization/dead-store bloat signature in the
  general `mir_try_emit_spilled_scalar_cfg` selector - this remains the
  single biggest unaddressed lever (see `mir-text-size-plan.md`'s "Root
  causes to close" list carried over from this session's plan).
- Combined byte-sum reduction from this vein's Items T1-T6:
  -1,542,587 bytes (~18.4%, T1-T4) plus Item T5's aggregate-return
  fix (which, uniquely among T1-T5, also moved coverage: +5 functions)
  plus Item T6's 3 landed struct-copy/assignment `ldir` fixes (0
  coverage change, byte reduction across the still-fallback
  population) across the whole corpus vs. the pre-T1 baseline, with 0
  real regressions (only digit-width text-metric artifacts from T2/T3,
  each independently confirmed non-real via a label/offset-normalized
  diff; T4/T5/T6 had 0 regressions outright). Item T6 also found and
  **deferred** a 2-site fix (`MIR_CALL`/`MIR_CALL_AGGREGATE`
  struct-argument-copy) that is provably beneficial in isolation but
  was withheld because it exposes a pre-existing, unrelated Root-Cause-C
  redundant-address-recomputation bug in the one function it currently
  affects (`tsretret.make_normal`) - see `mir-text-size-plan.md`'s
  Item T6 entry for the full stash-based A/B proof.
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

## Next session should

1. Execute the dedicated `text-size` plan (drafted this session,
   carried into `mir-text-size-plan.md`'s Execution Log after each
   item lands):
   - Tackle the single biggest lever: extend compare+branch
     fusion (the shape `mir_try_emit_comparison_branch` already does
     for a narrow whole-function pattern) into the general
     `mir_try_emit_spilled_scalar_cfg` path that 91% of the corpus goes
     through, where `mir_emit_scalar_compare` still unconditionally
     materializes and spills/reloads an explicit 0/1 boolean for every
     comparison. Stage narrowly: forced-accept-diff 2-3 representative
     functions first (`tesc::check_s`, `adaint::and_expr`), define the
     narrowest adjacency predicate, validate iteration-tier then
     milestone-tier (this touches the dominant selector).
   - Re-evaluate a complementary dead-store-elimination dccpeep pass
     after the selector-side fix lands (SKILL.md notes dccpeep's
     existing same-basic-block pass only removes one of the two
     store/reload round-trips from this pattern, leaving a second,
     genuinely dead store).
   - Continue Root Cause C's residual (non-adjacent single-use
     forwarding) once the above land and the far-bucket population has
     been re-measured. Fixing Root Cause C's redundant-address-
     recomputation bug would also unblock re-applying Item T6's
     deferred `MIR_CALL`/`MIR_CALL_AGGREGATE` struct-argument-copy
     `ldir` fix (see `mir-text-size-plan.md`'s Item T6 entry) as a
     direct bonus - check that first when starting Root Cause C work.
   - Re-run the full census and re-bucket the `text-size` gap fresh
     before picking each next item; the population shifts as items
     land (this session already caught one stale-ranking trap this
     way - the prior top outlier dropped out of the list entirely
     after Item T5 landed).
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
