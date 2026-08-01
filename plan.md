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
- Coverage: 190/2021 runnable functions MIR-accepted (9.40%).
- `text-size` fallback is still the dominant reason (1,749/2021, ~86%
  of the corpus, ~97% of all fallback) - see SKILL.md's "Known root
  cause" section and `mir-text-size-plan.md` for the full analysis.
- Combined byte-sum reduction from this vein's Items T1-T3:
  -1,470,674 bytes (~17.5%) across the whole corpus vs. the
  pre-T1 baseline, with 0 real regressions (only digit-width text-
  metric artifacts, each independently confirmed non-real via a
  label/offset-normalized diff).
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

## Next session should

1. Continue the `text-size` root-cause work per `mir-text-size-plan.md`:
   - Audit `mir_can_forward_stack_to_index`'s `!mir_virtual_iy_base`
     entry gate using the same git-blame + forced-accept-probe
     methodology as Item T3, to determine if it's also dead scaffolding
     or a genuine correctness dependency.
   - Investigate Root Cause C's residual (`mir_try_emit_spilled_scalar_cfg`
     gives every MIR value a fixed home slot and unconditionally
     stores/reloads it even with zero intervening side effects) beyond
     what the existing single-use HL-forwarding mechanism already
     covers - non-adjacent single-use values separated by a skippable
     label, or more complex live ranges.
   - Re-run the full census and re-bucket the `text-size` gap fresh
     before picking the next item; the population shifts as items land.
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
