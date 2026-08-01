# dcc MIR migration - working plan (session handoff)

Full narrative detail lives in `mir-migration-plan-to-100pct.md`'s
Execution Log. This file is a short, current-state pointer for picking
the work back up without re-reading the whole log.

## Where we are

- Branch: `perf/unified-regalloc`, pushed through Item 22.
- Coverage: 185/2018 runnable functions on `homed-scalar-cfg` +
  `general-rollout` + `comparison-branch` (9.17%), up from 159/2019
  (7.88%) at the start of this session's work (Items 21-22).
- All correctness and performance validation is clean: full 323-app
  `-Mode fast` safety net passes, 0 regressions.

## Recently landed (this session)

- **Item 21**: `MIR_STRING_ADDRESS` support in `homed-scalar-cfg`, plus
  3 latent bugs it exposed and fixed (dccpeep extrn/dead-code
  interaction, `_Bool` cast normalization gap, unprotected `MIR_LOAD`
  `HL` scratch clobber) and a 4th, more serious gap (no frame-space
  reservation for memory-resident locals - fixed by rejecting
  `mir.local_bytes != 0`, conservatively giving back 21 previously
  latently-unsafe functions).
- **Item 22**: `MIR_MEMBER_ADDRESS`, the constant-index subset of
  `MIR_INDEX_ADDRESS`, and a narrow (2-byte, no bitfield) subset of
  `MIR_LOAD_INDIRECT`, all sharing a new
  `mir_emit_pointer_offset_address_to_home()` helper. +30 functions net,
  0 regressions.

## Next recommended items (ranked by a fresh full-corpus disposable survey)

Re-run any time with the temporary-instrumentation technique documented
in the Execution Log (Item 22 entry) - re-survey before trusting these
numbers if more than one session has passed, since each landed item
changes the ranking underneath the next one:

1. **Variable-index `MIR_INDEX_ADDRESS`** (the subset Item 22 deferred):
   needs a `__mulu` runtime-call emission path for the stride multiply
   when the index isn't a compile-time constant. Real scope beyond a
   one-line acceptance-gate change.
2. **`MIR_STORE_INDIRECT`** (~4 hits at last survey, likely undercounted
   since it's rare in isolation but structurally required alongside
   `MIR_LOAD_INDIRECT` for any pointer-writes-through-pointer code):
   mirrors Item 22's `MIR_LOAD_INDIRECT` shape (write through an
   arbitrary homed pointer instead of read) - reuse the same
   `mir_home_color_live_across`-protected-scratch-through-`HL` pattern,
   since Z80 can only `ld (hl),r`/`ld (hl),n` for indirect stores.
3. **`MIR_COPY_AGGREGATE`** (~12 hits): struct/union assignment by value.
   Larger scope - needs a byte-range copy loop or `ldir`-style emission,
   not a single-instruction fold like the address opcodes above.
4. **`MIR_CALL_AGGREGATE`** (~2 hits): passing/returning structs by
   value through calls. Lowest yield of the currently-surveyed set: do
   last unless it turns out to gate a disproportionate number of whole
   functions once 1-3 are done.

## Non-negotiable process reminders (see SKILL.md for full text)

- Re-run the disposable opcode survey fresh before starting a new item
  in a new session - the ranking shifts as items land.
- Snapshot the census **before** editing every single time - Item 22's
  own accidental `case MIR_LOAD:` deletion was caught only because the
  before/after census comparison is mandatory, not by runtime testing.
- Every item needs: build clean -> regression-gated census
  (`--fail-on-regression`) -> focused `-Mode full` on the exact
  census-reported affected-app list -> a wide `-Mode fast` safety net
  across the full corpus -> Execution Log entry -> commit -> push to
  `origin/perf/unified-regalloc`.
- Never accept a perf baseline movement without confirming, per app,
  that it is explained by a proven-necessary correctness fix (or is
  genuine noise below ~0.1% offset by an equal-or-larger improvement in
  the same app) - never to "hide" a real regression.
- Clean up scratch census files (`build/mir-*.tsv`) and any `/tmp/`
  debugging artifacts before committing; they are not tracked and
  should not accumulate in `build/`.
