# dcc MIR migration - working plan (session handoff)

Full narrative detail lives in `mir-migration-plan-to-100pct.md`'s
Execution Log. This file is a short, current-state pointer for picking
the work back up without re-reading the whole log.

## Where we are

- Branch: `perf/unified-regalloc`, pushed through Item 23.
- Coverage: 185/2018 runnable functions on `homed-scalar-cfg` +
  `general-rollout` + `comparison-branch` (9.17%), up from 159/2019
  (7.88%) at the start of this session's work (Items 21-23).
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
- **Item 23**: `MIR_STORE_INDIRECT` (narrow 2-byte, non-bitfield write
  through an arbitrary homed pointer), mirroring Item 22's
  `MIR_LOAD_INDIRECT`. +0 functions unlocked, 0 risk - groundwork.
- **Item 24**: `MIR_COPY_AGGREGATE` (struct/union assignment by value).
  +0 functions unlocked, 0 census changes at all - groundwork.
- **Item 25 (deferred)**: `MIR_CALL_AGGREGATE` investigated and
  deferred, not implemented - too large/risky to port (cached/
  rematerialized-argument machinery + 3-way argument/return shape
  handling) for only ~2 survey-hit yield. A fresh full-corpus survey
  confirms **the opcode-recognition fallback reason is now retired as a
  lever**: 0 hits for any opcode other than `callagg`. The
  single-opcode admission vein (Items 9-24) is closed out.

## Where coverage growth must come from next

A fresh census after Items 22-25 confirms SKILL.md's already-documented
systemic root cause is still the dominant blocker, essentially
unchanged by this session's opcode work: `text-size` fallback is 1,756
of 1,833 (95.8%) of all fallback functions - a `spilled-scalar-cfg`
code-quality problem (unconditional 0/1 boolean materialization in
`mir_emit_scalar_compare`, `dcc_mir.c` ~line 4939, plus a dead-store
class `dccpeep`'s existing passes don't remove), not a `homed-scalar-cfg`
acceptance-gap problem. Coverage: 185/2018 (9.17%), unchanged by Items
23-25 (Item 22 was the last item this session with net new yield).

**This is a different, larger body of work than every item in this
plan document** (Items 9-25): it touches the shared
`mir_try_emit_spilled_scalar_cfg` selector every non-MIR function goes
through, not an isolated `homed-scalar-cfg` opcode admit. It should be
planned as its own dedicated multi-item effort (likely: (1) teach
`mir_emit_scalar_compare` to detect when its boolean result is
consumed only by a single `MIR_BRANCH_FALSE`/`MIR_BRANCH_TRUE` and skip
materializing the explicit 0/1 value in that case, fusing compare+
branch directly the way the narrow `mir_try_emit_comparison_branch`
selector already does for its own limited whole-function shape; and/or
(2) add a dead-store elimination pass, either as a new `dccpeep`
peephole or a MIR-level analysis, targeting stores that are provably
never reloaded anywhere in the function - a different bug class from
the same-basic-block redundant-reload pass `dccpeep` already has).

## Next session should

1. Start a fresh planning document for the `text-size` root-cause body
   of work (this document's single-opcode vein is retired, per Item 25's
   defer rationale - do not keep adding items here).
2. Re-verify the 95.8% `text-size` figure and the `mir_emit_scalar_compare`
   root cause with a fresh forced-accept diff on 1-2 representative
   functions before implementing, since it's been several items since
   SKILL.md's original 2026-07-30 measurement.
3. Design the compare+branch fusion or dead-store elimination as its
   own falsifiable hypothesis with a focused validation app, following
   the same discipline (snapshot before, regression-gated census,
   focused `-Mode full`, wide `-Mode fast` safety net) as every item in
   this document.

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
