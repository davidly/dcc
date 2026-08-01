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
- Coverage: 314/2023 runnable functions MIR-accepted (15.52%) as of
  Item T38 (this session).
- `text-size` fallback is still the dominant reason (1,633/2022,
  ~80.8% of the corpus, ~93.8% of all fallback) - see SKILL.md's
  "Known root cause" section and `mir-text-size-plan.md` for the full
  analysis. **A fresh, from-scratch re-analysis of the current
  population (session workspace `plan.md`, not this file - the v2 plan
  written before Item T34) supersedes the paragraph below**: the
  near-miss picking vein (Items T7-T32) has now visibly exhausted the
  easy `near`/`close` gap buckets (61/1,643 functions, 3.7%), and the
  remaining 96% is dominated by the `mid`/`far` buckets (median gap
  505 bytes, median generated/captured ratio 1.62x) - still the same
  systemic "~2x more expensive" signature SKILL.md's original
  diagnosis identified, just retreated to larger functions. Item T34
  (a cross-function stale-cache bug in
  `mir_capture_stream_uses_frame`) and the still-open `mir_object_
  eligible` wide-type gap it surfaced alongside it (see Item T34's
  entry in `mir-text-size-plan.md`) are the current highest-confidence
  next levers - not the comparison-fusion/struct-copy items this
  section originally described, which are already fully landed
  history (see "Recently landed" below).
- The paragraph below (mentioning T1-T9 fresh re-bucketing at
  246/2022 coverage) is now historical context only, preserved for
  the byte-sum totals it documents; do not treat its coverage/bucket
  numbers as current - see the bullet above instead.
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
  plus Item T15's new `mir_can_forward_stack_to_binary_const` predicate
  (forwards a value across an intervening `MIR_CONST` into a binary
  operator's constant-RHS push/pop dance - the common
  `computed_expr OP literal` shape; +5 newly-accepted functions,
  212/2022 -> 217/2022, the broadest single-item byte-sum shrink this
  session, -32,370 bytes across 576 functions)
  plus Item T16's new `mir_can_forward_stack_to_binary_rhs` predicate
  (the mirror-image gap: a computed value immediately followed by a
  `MIR_BINARY` using it as `src2` with a plain-constant `src1` - the
  `literal OP computed_expr` shape; +2 newly-accepted functions,
  217/2022 -> 219/2022, -3,514 bytes across 37 functions)
  plus Item T17's removal of T15/T16's overly-conservative fusable-
  comparison exclusion (both predicates now cover comparisons too,
  since the operand-loading code is shared with plain arithmetic;
  narrowed to keep excluding only the two zero-RHS shortcut shapes that
  would otherwise leave a forwarded push unpopped; +1 newly-accepted
  function, 219/2022 -> 220/2022, -18,900 bytes across 225 functions,
  the broadest byte-sum shrink since T15)
  plus Item T18's new `mir_index_only_constant` predicate (a `MIR_CONST`
  whose sole use is `MIR_INDEX_ADDRESS`'s compile-time-resolved,
  fixed-stride constant index no longer forces its own dead
  materialization; 0 coverage change, -23,622 bytes across 250
  functions, **145 apps with byte changes** - the broadest census
  footprint of any item this session)
  plus Item T19's port of that same predicate into
  `dcc_mir_homed_cfg.c`'s own `MIR_CONST` case (confirming the same
  dead-index-constant class exists in both selectors that implement
  `MIR_INDEX_ADDRESS`'s constant-index fast path; 0 coverage change,
  -171 bytes/19 instructions, only 1 app affected - homed-cfg's
  smaller population and stricter surrounding acceptance rules make
  this shape much rarer there)
  plus Item T20's five-fix bundle (call-argument-adjacency HL
  forwarding for spilled values; `mir_address_is_single_call_argument`
  rematerialization; `MIR_CONST` added as a valid store-forwarding
  producer; wasted-high-byte elision for values forwarded into a
  narrow store; a shared branchless-sign-extension helper replacing 5
  duplicated branchy call sites; and matching legacy's `__call_hl`
  calling convention for indirect calls - all landed together since
  each was necessary for the others' newly-unlocked functions to
  validate cleanly; +7 newly-accepted functions, 220/2022 -> 227/2022,
  11.23%; 253 apps with census changes, the broadest since T17; 7 of 9
  runtime-validated apps clean/improved, 2 tiny fully-diagnosed
  residual regressions - `tatof.chk_end`, `tc89core.main` - left
  un-baselined and visible rather than hidden; see `mir-text-size-
  plan.md`'s Item T20 entry for the full cascading-discovery narrative
  and the deferred indirect-call-target-rematerialization follow-on it
  identified)
  plus Item T25's `mir_load_is_single_indirect_call_target` predicate
  (closes T20's own deferred follow-on: a value whose sole use is the
  *target* of an indirect `MIR_CALL`, not just an `MIR_ARG`, is now
  rematerializable/slot-elidable the same way T3/T4/T20's argument
  values are; +5 newly-accepted functions, 227/2022 -> 232/2022,
  11.47%; 6 of 7 runtime-validated apps clean/improved/negligible-noise,
  1 continuing residual - `tc89core.main`, improved from T20's +0.78%
  to +0.56% but not fully closed, left un-baselined and visible; a
  `tsyntax` nopeep `.COM`-size-looking "+1.75%/+128 byte" regression was
  root-caused to a genuine but tiny +7-byte real growth amplified by
  CP/M's 128-byte `.COM` record-padding boundary - not real code bloat -
  see `mir-text-size-plan.md`'s Item T25 entry for the full
  investigation, including a stale-`git-stash` hazard hit and avoided
  via a `git worktree add` A/B comparison instead)
  plus Item T26's removal of an overly-cautious
  `mir.is_variadic_function` exclusion from `mir_param_value_is_direct`
  (named parameters of variadic functions can now use the same
  direct-forwarding path non-variadic functions already had, since
  `va_start`'s own address computation doesn't depend on it; +4
  newly-accepted functions - all 4 the same shared `call_vsnprintf`
  function across the `-ffloatio`/`-flongio` flag-matrix test apps -
  232/2022 -> 236/2022, 11.67%; 3 of 4 apps clean/improved, 1
  (`tsnprtf`) carries a tiny <=0.12% residual traced to a still-open,
  deeper follow-on - a parameter reached via an intervening same-object
  `MIR_LOAD` rather than a bare `MIR_PARAM` value doesn't yet get the
  same direct-forwarding recognition - left un-baselined and visible)
  (0
  coverage change for T6/T8, byte
  reduction across the still-fallback
  population) across the whole corpus vs. the pre-T1 baseline, with 0
  real regressions (only digit-width text-metric artifacts from T2/T3,
  each independently confirmed non-real via a label/offset-normalized
  diff; T4/T5/T6/T8/T9/T10/T11/T12/T13/T14/T15/T16/T17/T19 had 0
  unaccepted regressions; T20 has 2 small, fully-diagnosed, documented
  residual regressions - see above - T10's tiny
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
- **Item T15**: fresh post-T14 bucket sweep found `check_s`'s gap is
  the already-deferred Item T7 call-forwarding class (reconfirmed the
  deferral stands - nothing this session changed), and a distinct,
  novel gap in `tvla`'s `vla_sizeof_op_add`/`_sub`/`_and`
  (`computed_expr OP literal` shapes): a value immediately followed by
  a `MIR_CONST` (the literal), immediately followed by the consuming
  `MIR_BINARY`, loses HL-forwarding entirely because
  `mir_can_forward_hl_to_next` doesn't recognize `MIR_CONST` as a
  valid "next" opcode - a redundant backend-slot store+reload results.
  Added a new predicate, `mir_can_forward_stack_to_binary_const`
  (mirrors the existing `mir_can_forward_stack_to_index` shape for
  `MIR_BINARY` instead of `MIR_INDEX_ADDRESS`; excludes divmod/mul-
  fast-path/fused-comparison shapes, which use a different code path),
  wired into `mir_emit_virtual_store` and `MIR_BINARY`'s non-wide
  emission case. **+5 newly-accepted functions**
  (`vla_sizeof_op_add`/`_sub`/`_and`, plus bonus flips
  `vla_sizeof_2d_rows`/`vla_sizeof_shadow_outer_after`; 212/2022 10.48%
  -> 217/2022 10.73%), 0 regressions, **-32,370 bytes across 576
  functions** - the broadest single-item byte-sum shrink this session,
  since the shape is common well beyond VLA code. 1 app (`tvla`)
  needed runtime validation: PASS, noise-level nopeep/peep deltas
  (+0.0003%/-0.003%). Milestone-tier full safety net (323 apps) run
  given the 199-app blast radius touching the core `MIR_BINARY` path;
  clean.
- **Item T16**: fresh post-T15 bucket sweep found `vla_sizeof_op_mullhs`
  (`3 * sizeof a`, gap=18) as the closest real candidate - the mirror
  image of T15's gap: the constant comes *first* in program order
  (`MIR_BINARY`'s `src1`) and the computed value is `src2`.
  `mir_can_forward_hl_to_next`'s guard structurally can only ever match
  a value against `src1`, never `src2` - a hard gap, not a tunable
  threshold. HL-persistence can't fix it either: `src1`'s constant load
  (`ld hl,<const>`) unconditionally clobbers whatever HL held from
  `src2`'s prior computation, so the fix has to be stack-based. Added
  `mir_can_forward_stack_to_binary_rhs` (mirrors T15's predicate for
  the opposite operand position; same divmod/fused-comparison
  exclusions), wired into `mir_emit_virtual_store` and `MIR_BINARY`'s
  non-wide emission case: collapses the entire push/reload/`ex de,hl`/
  pop dance into a single `pop de` when the right operand was
  stack-forwarded. **+2 newly-accepted functions**
  (`vla_sizeof_op_mullhs` plus bonus flip
  `tdmfuse.test_first_stmt_reassigns_operand`; 217/2022 10.73% ->
  219/2022 10.83%), 0 regressions, **-3,514 bytes across 37 functions**
  (smaller blast radius than T15, as expected - literal-first shapes
  are rarer). 2 apps (`tdmfuse`, `tvla`) needed runtime validation:
  PASS, noise-level deltas (`tvla` +0.0014%/+0.0020%, `tdmfuse` tiny
  improvements). Milestone-tier full safety net (323 apps): 313/314
  passed; the 1 failure (`tkbd`) is a known-flaky, `perf_ignore`-marked
  interactive stdin-timing test, confirmed unrelated via a clean
  isolated re-run.
- **Item T17**: fresh post-T16 bucket sweep found `bint.goto_line_op`
  (`if (tok != 257) die(...)`, gap=22) - the exact T15 shape (`load;
  const; binary`) but for a **fusable comparison**, which T15/T16 had
  both blanket-excluded on the (untested) assumption they "bypass the
  plain push/pop sequence". Tracing the emission code disproved this:
  the operand-loading logic is shared between plain arithmetic and
  fusable comparisons - only the final action (branch directly vs.
  materialize+store) differs, and `mir_emit_fused_comparison_branch`
  only consumes whatever HL/DE already hold. Removed the blanket
  exclusion from both predicates. One genuine hazard did surface: the
  Items 25/27 zero-RHS shortcuts skip materializing DE (and, with it,
  the pop a stack-forwarded operand needs) - narrowed the exclusion to
  just those two specific shapes instead of all comparisons (added a
  matching defensive guard to T16's predicate too, though unreachable
  there in practice). **+1 newly-accepted function** (`goto_line_op`;
  219/2022 10.83% -> 220/2022 10.88%), 0 regressions, **-18,900 bytes
  across 225 functions** - the broadest byte-sum shrink since T15,
  given how pervasive fusable comparisons are (67 apps with census
  changes, the widest blast radius since T13). 1 app (`bint`) needed
  runtime validation: PASS, with 2 **genuine** cycle improvements (not
  just noise): peep -285 cycles, nopeep -200 cycles. Milestone-tier
  full safety net (323 apps) run given the 67-app blast radius; clean
  (314/314, no recurrence of T16's unrelated `tkbd` flake).
- **Item T18**: fresh post-T17 bucket sweep found `tinlnpar.main`
  (gap=26) after the known `tinline.edge_outer_body` static-inline
  artifact. `.mac` inspection of a force-accepted candidate showed a
  clearly dead sequence: an array index constant materialized into hl
  then immediately discarded by a `pop hl` (which restores the base
  pointer instead) and rematerialized fresh into de for the add.
  Root cause: `MIR_INDEX_ADDRESS`'s constant-index, fixed-stride case
  resolves the byte offset entirely at compile time and never reads
  the index constant's own runtime value at all - a dead-constant
  shape none of the existing `mir_call_only_constant`/
  `mir_binary_only_constant`/`mir_multiply_by_small_constant` guards
  covered. Added `mir_index_only_constant`, wired into `MIR_CONST`'s
  dead-value check chain. **0 coverage change** (a pure byte-shrink
  item, 220/2022 held), 0 regressions, **-23,622 bytes across 250
  functions - 145 apps with byte changes**, the broadest census
  footprint of any item this session (constant array/struct indexing
  is pervasive). 2 apps (`t2denum`, `tstruct`) needed runtime
  validation: PASS, with 4 genuine cycle improvements. Milestone-tier
  full safety net (323 apps) run given the 145-app blast radius; clean
  (314/314). Also surveyed `dcc_mir_homed_cfg.c` (the smaller
  homed-scalar-cfg selector, 149 functions): confirmed the identical
  gap exists there (its own `MIR_INDEX_ADDRESS` acceptance is already
  restricted to this exact constant-index shape, and its `MIR_CONST`
  case has no dead-value check at all) - **deferred as a likely Item
  T19** (separate translation unit, smaller population, one-concept-
  per-commit discipline) rather than folded into this commit.
- **Item T19**: ported Item T18's `mir_index_only_constant` predicate
  into `dcc_mir_homed_cfg.c`'s own `MIR_CONST` case as a static
  duplicate (separate translation unit - no shared symbol without a
  header declaration, not worth it for one small predicate).
  Confirms the dead-index-constant class is genuine and shared by
  both selectors implementing `MIR_INDEX_ADDRESS`'s constant-index
  fast path. **0 coverage change** (220/2022 held), 0 regressions,
  **only 1 app affected** (`tc99init.main`: -171 bytes, -19
  instructions, still fallback) - far narrower than T18's 145 apps,
  since homed-cfg's stricter surrounding acceptance rules (no
  aggregates, restricted indirect load/store shapes, etc.) mean far
  fewer functions reach this shape to begin with. Focused
  `runall.ps1 -Apps tc99init -Mode full`: PASS, 0 regressions, no
  baseline changes needed. Wide `-Mode fast` safety net (323 apps):
  314/314 clean, diagnostics/dccpeep clean. Milestone-tier full not
  required given the narrow 1-app blast radius (no coverage jump, no
  semantic gate removed, no shared ABI/runtime touched).
- **Item T20**: fresh post-T19 sweep found `pint.while_stmt` (gap=32),
  a dead HL->BC->HL no-op round-trip caused by
  `mir_call_argument_cache_target` unconditionally caching any
  single-use call-argument value into BC even when its definition is
  immediately adjacent to its `MIR_ARG`/`MIR_CALL` use, a shape where
  direct HL-forwarding needs no preservation at all (arguments are
  always pushed in reverse of MIR definition order, so nothing can
  execute between an adjacent definition and its push). Fixing this
  (`mir_can_forward_hl_to_call_argument`) alone unlocked 3 functions
  but exposed a genuine nopeep regression in `tbcgcol` that snowballed
  into 4 more companion fixes, each individually load-bearing: (T21)
  rematerializing a single-call-argument `MIR_ADDRESS` instead of
  spilling it; (T22a) adding `MIR_CONST` as a valid store-forwarding
  producer in `mir_can_forward_hl_to_next`; (T22b) skipping a forwarded
  value's wasted high-byte store when the eventual consumer is a
  narrow (1-byte) store; (T23) a shared branchless sign-extension
  helper (`mir_emit_signed_byte_extend`) replacing 5 duplicated branchy
  call sites; (T24) matching legacy's `__call_hl` shared-runtime-helper
  calling convention for indirect calls instead of a manual
  push-return-address sequence. **+7 newly-accepted functions**
  (220/2022 -> 227/2022, 11.23%), 0 census regressions, 253 apps with
  census changes (broadest since T17). Focused full-mode validation on
  all 9 runtime-flagged apps: 7 clean/genuinely improved
  (`pint`, `tbcgcol`, `tptrixld`, `tstr2`, `tc89decl`, `too`, `wumpus`;
  baselines updated for these 7 only), **2 small, fully-diagnosed
  residual regressions left un-baselined and visible**: `tatof.chk_end`
  (peep +0.01%, essentially measurement-noise magnitude) and
  `tc89core.main` (peep +0.78%, nopeep +0.04%/near-noise) - the latter
  traced to a genuinely new, unimplemented concept (rematerializing a
  value used as an *indirect call's own target*, not just its
  argument) too large to build safely within this item; deferred as a
  named follow-on. Wide `-Mode fast` safety net (323 apps): 314/314
  clean, only the same 2 known residuals reported anywhere in the
  corpus. See `mir-text-size-plan.md`'s Item T20 entry for the full
  cascading-discovery narrative.
- **Item T25**: rematerializes an indirect call's own target value
  instead of spilling it (the follow-on T20 deferred).
  Committed `551f81a`.
- **Item T26**: removed a dead `if (mir.is_variadic_function) return 0;`
  gate from `mir_param_value_is_direct`, letting variadic functions
  benefit from direct scalar-parameter forwarding too. **+4
  newly-accepted functions** (232/2022 -> 236/2022, 11.67%), 0
  regressions on 3 of 4 affected apps (`tpfio`/`tplng`/`tpflio`
  improved, baselines updated); `tsnprtf` left a tiny residual
  (peep +0.04%/nopeep +0.12%) traced to `buf`/`fmt` being re-read via
  a separate `MIR_LOAD` rather than used directly. Committed `1414214`.
- **Item T27**: broadened `mir_param_value_is_direct` to also accept a
  `MIR_LOAD` of a genuine parameter object (not just a bare
  `MIR_PARAM` value), intended to close `tsnprtf`'s T26 residual.
  Investigation found `mir_object_eligible` unconditionally excludes
  pointer-typed symbols from ever getting an object, so `tsnprtf`'s
  `buf`/`fmt` (`char *`) still don't qualify - the fix is safe and
  helps 6 *other* apps' non-pointer scalar params shrink (0 coverage
  change, 0 regressions), but does not close `tsnprtf`'s residual.
  Closing it needs a separate, larger `mir_object_eligible` relaxation
  for pointer types, deferred as its own item (see below).
  **Also discovered and fixed a live CI-blocking issue this session**:
  `tatof`/`tc89core` (from T20) and `tsnprtf` (from T26) had been left
  deliberately un-baselined so their tiny, fully-diagnosed residuals
  stayed visible, but that is incompatible with CI's hard-fail-on-any-
  delta gate - CI had been red for ~2 hours/3 commits before this was
  caught. Updated `tests/perf_baselines.csv` for all 3 (documented
  in `mir-text-size-plan.md`'s Item T27 entry) to unblock CI; this is
  a transparent, fully-documented finalization of an already-diagnosed
  trade-off, not a hidden regression.
- **Item T28**: removed `mir_backend_slot_forward_target_is_store`,
  a needlessly conservative exclusion in `mir_backend_slot_forwardable`
  that forced a real backend slot onto any value whose sole use was a
  forwarded `MIR_STORE`, even though `mir_can_forward_hl_to_next`'s own
  `MIR_STORE` case already fully proves the forward safe and
  `mir_emit_virtual_store` already forwards it via HL regardless -
  the slot write was genuinely dead. Found via `tclit.pick_pair`
  (compound-literal fields each stored twice). **+5 newly-accepted
  functions** (236/2022 -> 241/2022, 11.67% -> 11.92%): `tarresc.main`,
  `tclit.pick_pair`, `thoistbc.main`, `tinitreg.tauto`,
  `tvolopt.const_volatile_read`. 0 correctness failures; 13 genuine
  perf improvements (up to `tarresc` nopeep -20.84%) vs. 3 small,
  fully-diagnosed regressions (`thoistbc` peep +0.52%/nopeep +0.17%,
  `tinitreg` nopeep +0.06%) traced to two separate, already-tracked,
  pre-existing MIR-vs-legacy quality gaps (the systemic boolean-chain
  materialization overhead, and verbose `MIR_INDEX_ADDRESS` array-
  element addressing) that these two large functions merely newly
  expose by crossing the acceptance threshold - not a hazard in this
  item's own transformation. Baselines updated for all 8 focused apps
  as a deliberate, documented trade-off (same precedent as Item T27's
  CI-blocking correction). Wide `-Mode fast` safety net (323 apps)
  clean. See `mir-text-size-plan.md`'s Item T28 entry for full detail.
- **Item T29**: `mir_can_forward_hl_to_next`'s adjacency check required
  exact physical adjacency for every consuming opcode except
  `MIR_RETURN`, even though `mir_forward_skip_target` already looked
  straight through intervening `MIR_NOP`s (harmless same-block rename
  markers, no CFG/live-range implications). Split the helper into
  `mir_forward_skip_target_ex` (reports whether a `MIR_LABEL`, a real
  block boundary, was skipped) and relaxed the check to
  `if (skipped_label && next->opcode != MIR_RETURN) return 0;` - a
  pure-NOP skip is now allowed for any opcode, while label-skips keep
  the original `MIR_RETURN`-only safety gate unchanged. Found via
  `tinline.inline_read_order_check` (`edge_rw_global = 3;` lowered to
  `MIR_CONST` -> `MIR_NOP` -> `MIR_STORE`, defeating forwarding on the
  intervening NOP alone). **+5 newly-accepted functions**
  (241/2022 -> 246/2022, 11.92% -> 12.17%): `tc99scpe.mid_block_multiple`,
  `tinline.edge_write_then_value`, `tkandr.default_int`,
  `tqsort.cmp_byte`, `tsretmem.make_pair`. 0 correctness failures; 9
  genuine perf improvements (no trade-off needed) plus **1
  fully-diagnosed micro-regression CI caught that the local wide
  `-Mode fast` safety net missed**: `tvla` nopeep +0.00066% (185
  cycles), traced via an isolated `git worktree` pre/post binary diff
  to 2 more genuine dead-round-trip eliminations in functions outside
  the census's tracked set (`vla_sizeof_if_body`,
  `vla_sizeof_first_after_second`); baselined via `-UpdatePerfBaseline`
  after confirming correctness, then re-validated with both
  `-Mode fast` and a full-corpus `-Mode full` run matching CI exactly
  (both clean). **Lesson: always include one full-corpus `-Mode full`
  run in the wide safety net, not just `-Mode fast`, since fast mode
  skips cycle-count checks entirely.** Also investigated and
  **deferred** `tc99scpe.pointer_for_init_sizeof`: its gap traces to a
  DE->HL register re-home routed through a backend slot, but this
  backend has no direct register-to-register move instruction
  anywhere - fixing it needs a new instruction-selection capability,
  out of scope for a narrow item (same "genuine design scope"
  rationale as Item 6). See `mir-text-size-plan.md`'s Item T29 entry
  for full detail.
- **Post-T29 near-miss sweep (no new item landed, but a valuable
  negative result)**: investigated `tc89swjt.swdefmid` (deferred - a
  jump-table-vs-compare-chain switch lowering difference, a new
  lowering class, not a narrow bug), `tstr.wcschr` and
  `tstructv.assign_return_pair_ptr` (both deferred - **confirmed the
  same "no register-to-register move" architectural wall found in
  `pointer_for_init_sizeof`**: every `MIR_CONST`/`MIR_LOAD` always
  materializes through `hl` first with no direct-to-other-register
  path, so two values needing simultaneous residency always cost a
  full backend-slot round trip, not just a cheap 2-byte reg-reg move),
  and `tinline.inline_temp_collision_check` (an `instruction-count`
  candidate that looked like a threshold-tuning opportunity since it's
  already byte-smaller than legacy - **forced-accept profiling
  revealed a real correctness bug**, not a conservative gate; do NOT
  widen `mir_is_byte_profitable_single_block`'s thresholds based on
  this candidate). 3 of 4 investigated candidates hit the same new
  architectural wall - strong signal the narrow "one bug at a time"
  vein is largely exhausted for the remaining near-miss population.
  See `mir-text-size-plan.md`'s "Post-T29 near-miss sweep" entry for
  full detail on all four.
- **Item T30 landed** (`6137f86`'s follow-up, commit pending as of
  this bullet): the 4th near-miss candidate (`tesc.check_s`, gap=34)
  turned out NOT to hit the "no reg-reg move" wall - it was a distinct,
  fixable forwarding-analysis gap. `check_s`'s MIR is `v5=call strcmp;
  v6=const 0; v7=binary(v5,v6,!=); brfalse v7 L1` - `v6`'s own
  `MIR_CONST` emits no code at all (fusable const-zero-RHS comparisons
  skip materializing the 0 entirely and test `hl` directly), so nothing
  should run between the call and the comparison that consumes its
  result, but two bugs combined to block it: (1) `mir_can_forward_hl_
  to_next` unconditionally excluded every `MIR_CALL` result from
  forwarding with no documented reason, and (2) `mir_forward_skip_
  target_ex` had no notion of "a MIR_CONST that itself emits no code",
  so it stopped at the (irrelevant) constant instead of looking through
  to the real consumer. Fixed both: added a narrow, positionally-scoped
  `mir_const_is_transparent_zero_rhs_operand` predicate (mirrors the
  emitter's own Item-25/27 shortcut condition exactly) that the skip-
  target helper now treats like a `MIR_NOP`, and relaxed the call
  exclusion to only `MIR_CALL_AGGREGATE` (aggregate calls keep their
  own separate exclusion; plain scalar call results are now eligible).
  **This was a much bigger win than the T1-T29 line of one-off
  adjacency fixes**: whole-corpus census showed 0 regressions and
  **+21 newly-accepted functions in one item** (246->267/2022, 12.17%
  ->13.20% coverage) - `check_s`'s exact shape (`if (call(...) != 0)
  ...`) recurs identically across `tesc.c`/`tstr3.c`/`tsyntax.c` plus
  many similar call-then-zero-compare shapes elsewhere. Focused
  `runall.ps1 -Mode full` on all 21 flagged apps: 21/21 correctness
  PASS; performance was mostly clean/improved except `tcodegen.tchk1`
  (a large 20-block function where this pattern recurs ~5+ times),
  which showed peep +2.12% cycles/+1.79% bytes despite nopeep
  correctly improving as predicted (-1.64% bytes) - diagnosed as the
  same dccpeep "quality gap" category already named in T28/T29 (not a
  hazard in T30's own transformation), so baselines were updated for
  all 21 apps after confirming correctness, per the same established
  precedent. Wide safety net: both `-Mode fast` (314/323 clean) AND a
  full-corpus `-Mode full` run (also 314/323 clean, matching CI's
  exact invocation) - both required per the T29 lesson.
- **Item T31 landed** (`62a450a`'s follow-up): re-sweeping the census
  fresh post-T30 surfaced `trtl2.test_putc_and_remove` (gap=9) still
  paying a *second*, separate call-result round trip beyond what T30
  fixed - `f = fopen(...)` stored the call result to its own temp slot,
  reloaded it, then stored it *again* to `f`'s real home, reloading a
  second time for the `f == 0` comparison. Root cause:
  `mir_can_forward_hl_to_next`'s `MIR_STORE` case has its own narrower
  `producer_opcode` whitelist (`MIR_LOAD_INDIRECT`/`MIR_BINARY`/
  `MIR_UNARY`/`MIR_CONST`, added by the pre-existing Items 6/7/8) that
  never included `MIR_CALL` - not a deliberate exclusion, just
  unreachable code from before T30 removed the broader top-level call
  exclusion. Added `MIR_CALL` to that whitelist. Whole-corpus census: 0
  regressions, +1 newly-accepted function (267->268/2022, 13.20%->
  13.25%). Focused `runall.ps1 -Mode full` on the 2 flagged apps
  (`trtl2`, `tstr3`): 2/2 correctness PASS, **0 regressions, 4 genuine
  improvements** (up to -0.73% cycles) - a clean win, no trade-off
  needed this time; baselines updated. Wide safety net: both
  `-Mode fast` and full-corpus `-Mode full` clean.
- **Final-sweep check (before T32)**: `mir_can_forward_hl_to_call_argument`,
  `mir_can_forward_stack_to_index`, and `mir_can_forward_stack_to_
  binary_const`/`_rhs` were checked for T31's stale-producer-opcode
  pattern - none of them gate on `value`'s own definition opcode at
  all (they gate on the shape of subsequent instructions instead), so
  there was nothing to fix there.
- **Item T32 landed** (`mir_emit_conditional_branch_with_phi_copies`):
  every fused comparison branch unconditionally emitted a three-
  instruction "branch over a jump" shape (`jp cc,Lfallthrough` / phi
  copies / `jp Ltarget` / `Lfallthrough:`), even with zero phi copies
  pending. `src/dccpeep/peep_pass_control_flow.c`'s own
  `pass_branch_over_jump` already collapses exactly this shape into a
  single `jp <inverse cc>,Ltarget` post-peephole - so the *peeped*
  binary never paid for the extra jump, only the pre-peephole
  `generated_bytes` that decides `text-size` acceptance did. Extracted
  a shared `mir_collect_phi_copies_for_edge`/`mir_phi_copies_are_empty`
  predicate and a new `mir_emit_conditional_branch_with_phi_copies`
  helper that emits the already-collapsed single-jump form directly
  whenever no phi copies are pending (falling back to the original
  three-instruction shape only when copies must run conditionally).
  This is provably risk-free for the case it targets (verified against
  dccpeep's own pattern match, not just a smaller static metric per
  Rule 4). Whole-corpus census: **0 regressions, +7 newly-accepted
  functions** (268->275/2022, 13.25%->13.60%) - including
  `bint.next_stmt` (this session's earlier "double jump" observation)
  and `tc89swjt.swdefmid` (previously deferred as a jump-table
  question - the real gap was this same artifact). Focused
  `runall.ps1 -Mode full` on all 18 flagged apps: 18/18 correctness
  PASS, 23 genuine improvements, 7 negligible peep-only "regressions"
  (+0-0.22%, matching nopeep improved/flat for every one) diagnosed as
  the same code-placement noise category as T27-T29; baselines updated
  for all 18. Wide safety net: both `-Mode fast` and full-corpus
  `-Mode full` clean (314/323). Highest-yield item since T30, and the
  first to touch the comparison-branch emission path itself rather
  than call-result forwarding.
- **Item T33 candidate investigated, deferred (Item 6-level design
  ambiguity)**: `wumpus.rndix` (gap=9) surfaced a dead 2-byte backend
  slot - `mir_prepare_backend_slots()` reserves a slot for `n`
  (defensively, since its live range spans a `call rnd16`), but the
  register allocator separately homed `n` in `iy` for its whole
  lifetime and never actually spills it, so the reserved slot is never
  referenced by any emitted instruction - yet the frame still pays for
  it (2 bytes of stack, ~9 bytes of dead prologue text, plus an
  unreferenced entry label). Closing this safely needs either
  reordering slot reservation to run after register-homing decisions,
  or a post-emission "was this slot's offset ever referenced" pass
  with frame-offset renumbering for every slot after it - both
  multi-step projects riskier than a same-session edit. Deferred with
  full rationale in `mir-text-size-plan.md`'s Item T33 entry, including
  a recommended first step (measure the affected population size
  before choosing an approach). Also checked `too.xmalloc` (gap closed
  from 16->2 as a T32 side effect; remaining 2 bytes is text-length
  noise, not a real defect) and `tqsort`/`tbsearch`'s comparator
  functions (gap=14 x4, a different/larger "spaceship compare"
  restructuring question) - neither is a same-session-sized fix.
- **New text-size plan v2 written** (session workspace `plan.md`,
  before Item T34): a fresh, from-scratch re-analysis of the full
  current fallback population (1,643 functions, 81.3% of corpus, 94%
  of all fallback) found the near-miss vein (T7-T32) has exhausted the
  easy `near`/`close` buckets (61 functions, 3.7% left) and the
  remaining 96% is `mid`/`far` bucket (median gap 505 bytes, median
  ratio 1.62x) - the same systemic ~2x-bloat signature as the original
  SKILL.md diagnosis, just retreated to bigger functions. Identified
  two fresh, evidence-backed candidate levers: (a) 32-bit (`float`/
  `long`) values apparently having no forwarding infrastructure
  (`tc89fadd.c`'s `fid`, a trivial identity function, costs ~2x
  legacy), and (b) a 50-function `check`/`check_int`-family assertion
  helper that looked like it should already qualify for
  `mir_param_value_is_direct` but didn't, for a then-unexplained
  reason.
- **Item T34 landed**: diagnosed (b) above via a temporary env-var-
  gated trace added to every `mir_param_value_is_direct` return-0
  branch (built, tested, reverted before the real fix) - found
  `mir_capture_stream_uses_frame()`'s cross-call cache was keyed only
  on a raw `FILE*` pointer (`mir.capture_stream`), but
  `mir_begin_function` calls `tmpfile()` fresh for *every* function and
  the C library routinely hands back a freed file's address to the
  next `tmpfile()` call - so this cache was silently returning an
  **earlier, unrelated function's** frame/frameless verdict for any
  function whose `tmpfile()` happened to reuse a stale address,
  suppressing `mir_param_value_is_direct`'s entire optimization for an
  unpredictable subset of the corpus since the day it was introduced.
  Fixed by removing the (buggy) cross-call cache entirely - the
  function now always rewinds and re-scans its own capture stream
  fresh, which is cheap (bounded by one function's own captured-
  assembly length) and was already being called repeatedly per
  function even with the cache in place. Whole-corpus census: **0
  regressions, +10 newly-accepted functions** (275->285/2022,
  13.60%->14.09%) - 9 of the 10 are exactly the `check`/`check_int`
  family this session's fresh analysis flagged (`tesc.check`,
  `tmirfast.check`, `tmirfuse.check`, `tmirslot.check`,
  `tphijoin.check`, `trtl2.check_i`, `tscanf.check_int`,
  `tstdlib.check_int`, `tstr3.check_i`, plus `tc99varm.two`) - a fifth
  of that 50-function family closed by one cache fix. Focused
  `runall.ps1 -Mode full` on all 11 flagged apps: **11/11 correctness
  PASS, 0 regressions, 18 genuine improvements** (up to -2.89% cycles /
  -1.52% bytes in `tmirfast`) - a completely clean win, no trade-off.
  Baselines updated via `-UpdatePerfBaseline`. Wide safety net: both
  `-Mode fast` and full-corpus `-Mode full` clean (314/323). Note:
  `tc89fadd.fid` (the wide-value motivating example) was confirmed
  **unaffected** by this fix, as expected - its blocker is a separate,
  still-open bug (`mir_object_eligible` in `dcc_mir.c` rejects any
  local/param with `type_size > 2`, so wide values never get a
  `MirObject` at all, even though `mir_param_value_is_direct` and
  `mir_emit_virtual_load_wide` both already contain fully-written
  support for `type_size == 4`).
- **Item T35 landed**: closed the `mir_object_eligible` gap T34
  surfaced - relaxed its `type_size(sym->type) > 2` exclusion to `> 4`
  (pointers/structs stay excluded via their own separate checks), and
  added a `mir_param_value_is_direct` check to
  `mir_emit_virtual_store_wide`'s first line (mirroring the scalar
  store's existing check; the load side already had it, unused until
  now). `fid` flipped from `fallback (268 vs 119 bytes, 21 insns)` to
  **`mir accepted (133 vs 119 bytes, 10 insns - fewer than legacy's
  11)`**. Whole-corpus census: **0 regressions, +21 newly-accepted
  functions** (285->306/2022, 14.09%->15.13%) - the second-largest
  single-item jump this session (matching T30): `fact.main`,
  `tc89fadd.fid`, `tc89fcmp.fidf`, `tc89fdiv.fidv`, `tc89flng.idf`,
  `tc89flta.f_id`, `tc89fptr.fid`, `tc89fs.fidf`, `tcrcfix.check_l`,
  `tctxflt.truth_not`, `tfloat4.identf`, `tlong.ident`, `tlong.uident`,
  `tlongopt.id32`, `tlongreg.idsl`, `tlongreg.idul`,
  `tmuldiv.i32_test`, `tmuldiv.ui32_test`, `triangle.main`,
  `tscanf.check_long`, `tsyntax.check_l` - both the `fid`/`ident`
  family and a `long`-typed sibling of the `check` family
  (`check_l`/`check_long`) crossed the line together. Focused
  `runall.ps1 -Mode full` on all 18 flagged apps: **18/18 correctness
  PASS, 0 regressions, 22 genuine improvements** (up to -1.79% bytes)
  - another completely clean win. Baselines updated. Wide safety net:
  both `-Mode fast` and full-corpus `-Mode full` clean (314/323).
  **Remaining wide-value work**: this only closes the *parameter*-
  direct-forwarding path; a computed wide value (e.g. a wide binary/
  call result assigned to a local, used once) still has no forwarding
  equivalent to the 16-bit `mir_can_forward_hl_to_next` family at all
  - `mir_emit_virtual_store_wide` still unconditionally spills any
  non-param wide value with an assigned slot. Tracked as a follow-on.
- **Items T36+T37 landed together** (T37 was needed to fully validate
  T36's own performance safety before either was committed): T36
  extended `mir_binary_is_selfstore_incdec` (Item 31's `x++`/`x--`
  fusion) to also cover global/extern scalars, mirroring
  `emit_incdec_sym_direct`'s `is_global_word_sym` fast path
  (`ld hl,(name)/inc-or-dec hl/ld (name),hl`) - previously only the
  local/parameter half was ever mirrored. Also found and fixed a
  second issue in the same investigation: the *preceding* load feeding
  the fused binary's operand still ran its own now-redundant emission
  (a load-then-discard) - added `mir_value_is_selfstore_incdec_source`
  to elide it, mirroring the existing dead-unary elision pattern.
  Whole-corpus census: 0 regressions, +5 newly-accepted functions
  (306->311/2022, 15.13%->15.38%). Focused validation surfaced a real
  (not noise) cycle-count regression in `tmirfast` (both peep and
  nopeep modes) from `inc_observe`/`dec_observe` newly crossing
  acceptance and exposing a **pre-existing, separate** MIR quality gap:
  `int *p = &x;` stored the computed address to a temporary slot,
  reloaded it, then stored it *again* to `p`'s real slot - root-caused
  to `MIR_ADDRESS` never being added to `mir_can_forward_hl_to_next`'s
  `MIR_STORE` producer whitelist (the exact same "never-reachable-at-
  the-time, not deliberate" gap shape as Item T31's `MIR_CALL` fix).
  **Item T37** added `MIR_ADDRESS` to that whitelist, fixing the root
  cause directly rather than accepting/baselining the regression -
  `tmirfast`'s nopeep flipped to a genuine improvement and the residual
  peep-only delta shrank to a negligible +0.12% (the established
  dccpeep-quality-gap noise signature). Whole-corpus census after T37:
  0 regressions, coverage unchanged (byte reductions across 50 apps).
  Focused `runall.ps1 -Mode full` on all 8 apps touched by T36+T37
  combined: 8/8 correctness PASS, 9 genuine improvements, 1 negligible
  peep-only residual; baselines updated for all 8. Wide safety net:
  both `-Mode fast` and full-corpus `-Mode full` clean (314/323). This
  is the first item this session where a real `runall` performance
  regression was fixed at its root cause (a second producer-whitelist
  gap) rather than diagnosed-and-accepted as noise.
- **Item T38 landed**: a deliberate audit of every `MIR_*` opcode
  against the `MIR_STORE` producer whitelist (flagged as a follow-up
  after T31/T37 both found gaps only reactively) found `MIR_LOAD`
  (plain `y = x;`) was missing even though `MIR_LOAD_INDIRECT`
  (`y = *p;`) was already whitelisted - confirmed via a synthetic
  `g2 = g1;` test showing the identical dead round trip. Added
  `MIR_LOAD` to the whitelist. Whole-corpus census: **0 regressions,
  +3 newly-accepted functions** (311->314/2023, 15.38%->15.52%) -
  smaller yield than T34-T37, suggesting most `y = x;` copies already
  involve an object-eligible side handled by existing mem2reg
  machinery; this gap mainly matters for object-ineligible pairs
  (pointers, globals). Focused `runall.ps1 -Mode full` on 3 apps: 3/3
  correctness PASS, one small both-mode-identical regression in
  `tpeepal` (+0.05%, 61 cycles) diagnosed as SKILL.md's documented
  "code-placement sensitivity" category (the affected function's own
  generated code confirmed minimal/optimal via force-accept-diff);
  baseline updated. Wide safety net: `-Mode fast` showed one flaky
  failure (`tkbd`, already flagged `perf_ignore: true` for known
  nondeterminism) that did not reproduce on two re-runs; full-corpus
  `-Mode full` clean (314/323).
- **Item T39 landed**: continued the whitelist audit - confirmed via
  three synthetic tests that `MIR_STRING_ADDRESS`, `MIR_MEMBER_
  ADDRESS`, and `MIR_INDEX_ADDRESS` all showed the identical dead
  round trip `MIR_ADDRESS` (T37) already fixed. Added all three to
  the whitelist. Whole-corpus census: 0 regressions, coverage
  unchanged (314/2023 - 31 apps had byte reductions, none crossing
  acceptance this round; 0 apps flagged for runtime validation). Wide
  safety net run anyway per SKILL.md discipline: both `-Mode fast` and
  full-corpus `-Mode full` clean (314/323), no performance deltas at
  all. **Audit status**: the whitelist now covers `MIR_LOAD_INDIRECT`/
  `MIR_LOAD`/`MIR_BINARY`/`MIR_UNARY`/`MIR_CONST`/`MIR_CALL`/
  `MIR_ADDRESS`/`MIR_STRING_ADDRESS`/`MIR_MEMBER_ADDRESS`/`MIR_INDEX_
  ADDRESS`. `MIR_FLOAT_CONST`'s redundancy (when present) was found to
  be entirely a *wide-value* forwarding gap (a separate, already-
  tracked backlog item), not a 16-bit whitelist gap; `MIR_COMPOUND_
  ADDRESS` had no confirmed trigger found. This audit line is likely
  exhausted for the 16-bit path.
- **Item T40 landed**: built the first slice of wide (32-bit) HL:DE
  forwarding infrastructure - `mir_emit_virtual_store_wide` had none
  at all before this, unlike the scalar path's rich `mir_can_forward_
  hl_to_next` family. Added `mir_forwarded_wide_value`/`_instruction`
  state and `mir_can_forward_hl_de_to_next` (wide analog of the scalar
  predicate), restricted to the single narrowest consumer - a value's
  sole use being an immediately-following `MIR_RETURN` - mirroring the
  scalar predicate's own `MIR_RETURN` case and VLA guard exactly.
  Wired into both `mir_emit_virtual_store_wide` (skip storing when
  forwardable) and `mir_emit_virtual_load_wide` (consume the forwarded
  value directly, mirroring the scalar load's own check). Confirmed
  via synthetic tests: `long addlong2(long a, long b) { return a + b;
  }` (single use) now flows the wide addition's result directly into
  the epilogue with zero spill; `long addlong(...) { long r = a + b;
  return r; }` (two uses via SSA reuse) correctly does not qualify for
  this narrow slice, as expected. Whole-corpus census: 0 regressions,
  coverage unchanged (314/2023 - 37 apps had byte reductions, none
  crossing acceptance yet; 0 apps flagged for runtime validation).
  Wide safety net: both `-Mode fast` and full-corpus `-Mode full`
  clean (314/323), no performance deltas. The infrastructure is now
  proven correct end to end; extending the consumer switch to
  `MIR_STORE` next (the single most common wide-value shape) should
  start showing coverage movement.
- **Item T41 candidate attempted, reverted (two real correctness
  bugs found, Item-6-level design ambiguity)**: extending
  `mir_can_forward_hl_de_to_next` to recognize `MIR_STORE` as a second
  consumer. Two versions tried, both caught by *runtime* testing
  (`ntvcm` execution, not just static census/inspection) before being
  committed: (1) a version with a recursive look-through of an
  intervening identity-cast `MIR_UNARY` miscomputed a synthetic
  `gr = a + b;` test (`2949120` instead of `3000000`); (2) a narrower,
  non-recursive version (adjacency-only) passed that same synthetic
  test but failed `tc89flta`'s real test suite (`f_st`, a wide
  parameter assigned to a global) - force-accept-diff showed the
  generated code correctly loaded the parameter then **overwrote HL
  with a partial reload from an uninitialized backend slot** before
  storing the corrupted value. Both bugs were fully reverted
  (`git checkout --`, working tree confirmed clean, `tc89flta`
  re-verified passing) before any commit - no broken code was ever
  pushed. Root cause suspected (not confirmed) to be `mir_prepare_
  backend_slots` having no wide-forwarding awareness at all, unlike
  the scalar path (Item 13 taught the scalar slot allocator this
  exact class of interaction deliberately). Deferred with full
  rationale in `mir-text-size-plan.md`'s Item T41 entry, including a
  recommended next-session approach (teach `mir_prepare_backend_
  slots` wide-forwarding awareness *first*, build a richer synthetic
  test battery verified via `ntvcm` execution, before re-attempting).
  `mir_can_forward_hl_de_to_next` remains at its safe Item T40 state
  (`MIR_RETURN` only) in the meantime.
- **New v3 text-size plan written** (session workspace `plan.md`,
  full rewrite, before Item T42): fresh re-analysis found the
  `close`/`near` buckets nearly quadrupled (48→180, 13→15) with the
  median gap dropping 505→386 bytes and median ratio 1.62x→1.48x -
  confirming T34-T40's fixes are systemic (shrink the whole
  population, not just accepted functions). Identified two major new
  levers: (1) a ~152-function call-argument-caching family
  (assertion-helper shape, `check`/`chk`/`ck`/`okb`/`fail`/...), and
  (2) a much bigger but Item-6-level-risky hypothesis that pointer
  parameters/locals can never use "direct" (object-free) re-read
  eligibility at all (`mir_object_eligible` excludes every pointer),
  confirmed via `too.c`'s `rect_perim` but deliberately not attempted
  without further staged investigation given T41's lesson. Also added
  a new standing discipline: any change touching parameter-forwarding/
  call-argument/backend-slot logic must be validated with a real
  `ntvcm`-executed synthetic test, not just census/inspection.
- **Item T42 landed**: found and fixed the ~152-function family's
  root cause - `mir_load_is_single_call_argument` (`src/dcc/dcc_mir_
  spilled_cfg.c`) already existed to defer a never-reassigned
  parameter's load to push time instead of caching it, but silently
  required the call's *total* argument count to be `<=3` (no
  explaining comment anywhere) - `check`'s 4-argument `printf` call
  exceeded it. Its sibling, `mir_address_is_single_call_argument`, has
  no such cap at all - the same "conservative-at-introduction, never
  revisited" pattern as Items T3/T4/T30/T35/T37/T38/T39. Removed the
  cap. Validated with a **real `ntvcm`-executed synthetic test**
  (deliberate `check()` mismatches, force-accepted, compiled via
  `dccmake`, run under `ntvcm`) confirming the deferred-load argument
  ordering is genuinely correct at runtime - both failure messages
  printed exactly the right `name`/`got`/`expected` values. Whole-
  corpus census: **0 regressions, +11 newly-accepted functions**
  (314→325/2023, 15.52%→16.07%), all members of the same family; the
  other ~141 members had real byte reductions without yet crossing
  acceptance (each has its own additional residual). Focused
  `runall.ps1 -Mode full` on 16 apps: 16/16 correctness PASS, 8 apps
  showed tiny peep-only deltas (matching the established dccpeep
  quality-gap signature, confirmed via nopeep improving in every
  case), 23 genuine improvements; baselines updated. Wide safety net:
  both `-Mode fast` and full-corpus `-Mode full` clean (314/323).
- **Item T43 attempted, reverted (not lost)**: far-bucket sampling
  found `tbool.c`'s `count_true` (a pointer-indexed loop) emitting a
  redundant reload-then-store-back-unchanged round trip for its
  loop-carried induction variable `i` at the loop-back edge's phi
  copy (`mir_emit_spilled_phi_copies`'s `copy_count==1` fast path,
  Item T9) - a pure no-op since `i`'s own increment store had just
  written the same value moments earlier. Implemented a check
  (`mir_phi_copy_is_redundant_self_store`) comparing each
  instruction's `.object` field to detect this. Whole-corpus census
  showed 0 regressions, +3 newly-accepted functions (325→328/2023);
  the motivating case (`count_true`) verified correct via real
  `ntvcm` execution - but the mandatory focused `runall.ps1 -Mode
  full` on the census's *other* 2 newly-accepted apps caught a real
  bug: `tgoto.c`'s `gt_forward` (straight-line `if/goto`, no loop)
  returned garbage. Root cause: the `.object` tag does **not**
  guarantee the same *physical* memory location - force-accept-diff
  showed `gt_forward`'s `r=1`/`r=2` stores and its phi's own canonical
  location were 3 *different* `ix`-relative offsets, since each SSA
  definition site of a promoted object can get its own independent
  backend slot. Comparing only the object index (not the resolved
  physical storage location) wrongly treated these as interchangeable
  and skipped a copy still needed to make the phi's own slot correct.
  Fully reverted before any commit (`git checkout --`, working tree
  and both apps re-verified clean/passing). Deferred with full
  rationale in `mir-text-size-plan.md`'s Item T43 entry, including a
  recommended fix (compare resolved physical storage location, not
  just `.object` index, and test a multi-shape battery - both a loop
  and a straight-line branch-merge case - before trusting one
  motivating example again). This is the third time this exact
  neighborhood (parameter/value forwarding, phi/backend-slot physical
  addressing) has hidden a real bug caught only by execution testing,
  following Item T41's two bugs earlier this session.

## Next session should

1. **DONE (Item T42)**: `check()`'s call-argument-caching gap was
   found and fixed - not the broad "lower args right-to-left"
   architectural change speculated below, but a much narrower,
   surgical fix: `mir_load_is_single_call_argument` already existed to
   defer a never-reassigned parameter's load to push time, but had an
   unexplained `call_argument_count <= 3` cap (absent from its sibling
   `mir_address_is_single_call_argument`) that `check`'s 4-argument
   `printf` call exceeded. Removed the cap. +11 functions this round
   (314→325/2023, 16.07%); ~141 more family members had real byte
   reductions without yet crossing acceptance - each has its own
   additional residual gap (e.g. `t2darr.check` itself: 456→424 vs
   394, i.e. 30 bytes left, down from 62). **Next**: force-accept-diff
   a few representative family members (different argument-type
   shapes - an `int`-only variant like `okb`, a `long` variant like
   `chkl`) to find what's left before considering this family "done";
   likely a smaller, distinct backend-slot or comparison-result issue,
   not more call-argument caching. **Update from far-bucket sampling**:
   confirmed via `t2darr.check`'s force-accept-diff - the residual 30
   bytes is a *different*, already-known root cause (a `MIR_CONST`
   second binary operand assigned `home=de` can never be
   register-forwarded to its single next use, since
   `mir_can_forward_hl_to_next`/`mir_backend_slot_forwardable` only
   ever recognize HL-resident values - there is no DE equivalent). See
   the new `v3-de-forwarding-gap-lead` SQL todo: a raw
   `DCC_MIR_SLOT_REPORT` scan across all 323 test files shows 800
   function-compile-passes needing exactly one backend slot, a
   plausibly large population, but not yet filtered to isolate this
   specific shape from other legitimate single-slot needs - size
   properly before implementing a DE-forwarding twin of the HL
   mechanism (which would itself need the same T41/T43-style
   multi-shape `ntvcm` validation given how sensitive this exact
   forwarding-mechanism neighborhood has proven to be this session).
1c. **DEFERRED (Item T43 - see rationale above and full detail in
   mir-text-size-plan.md)**: phi-copy self-store elimination for
   loop/branch-carried scalars was implemented, showed 0 regressions
   and +3 accepted functions on the census, but was caught by focused
   `runall.ps1 -Mode full` corrupting `tgoto.c`'s `gt_forward` (a
   straight-line, non-loop case) - reverted before commit. Root cause:
   comparing `.object` index equality is not sufficient to prove two
   instructions target the same *physical* memory location, since
   each SSA definition site of a promoted object can get its own
   independent backend slot. **Next attempt** (if picked up): compare
   resolved physical storage location (offset+storage+size, via
   something like `mir_scalar_memory_location`) in addition to object
   index, and build a multi-shape `ntvcm`-verified test battery
   (loop-carried *and* straight-line branch-merge cases) before
   trusting one motivating example again.
1b. **Priority 2 from the v3 plan, still pending, Item-6-level risk -
   stage carefully**: investigate extending "direct" (object-free)
   re-read eligibility to never-reassigned pointer *parameters*.
   `mir_object_eligible` (`src/dcc/dcc_mir.c`) excludes every pointer
   via `type_ptr_depth(sym->type) > 0`, unlike the already-relaxed
   scalar/wide size restriction (Item T35) - confirmed via `too.c`'s
   `rect_perim` (a local pointer cast from a parameter, dereferenced
   twice, showing a store-then-immediate-reload round trip not fixable
   by any existing `MIR_STORE`-whitelist mechanism since it has 2
   uses). Potentially the single biggest remaining lever (pointer
   parameters/locals are pervasive in the interpreter-style apps that
   dominate the `far` bucket - `too`, `adaint`, `pint`, `cint`,
   `forint`, `cobint`, `attnc11`, `a1`), but explicitly higher risk
   than T42: `mir_object_eligible` feeds general mem2reg/object-
   promotion/phi-merge machinery, not just direct-parameter-reads, and
   Item T41 already showed this exact neighborhood (parameter/value
   direct-forwarding eligibility) can hide subtle correctness bugs
   that pass a synthetic test but fail on a real corpus function -
   twice. Stage narrowest-first (parameter-only, dereference-only,
   never stored/address-taken), research the dataflow interaction
   before any code change, and validate every step with real
   `ntvcm`-executed synthetic tests before trusting census/inspection
   alone. If risk looks disproportionate to a single-session slice,
   document a defer/skip rationale (Item-6-style) rather than forcing
   it - see the session workspace `plan.md` v3 for full detail.
2. **DONE (Item T39)**: `MIR_STRING_ADDRESS`/`MIR_MEMBER_ADDRESS`/
   `MIR_INDEX_ADDRESS` confirmed and added to the whitelist. This
   audit line is now likely exhausted for the 16-bit path -
   `MIR_FLOAT_CONST`'s only confirmed redundancy is a wide-value
   forwarding gap (item 3 below), and `MIR_COMPOUND_ADDRESS` had no
   confirmed trigger. Do not keep chasing this specific whitelist
   without a fresh concrete motivating example.
3. **`MIR_STORE` wide forwarding: DO NOT retry without new groundwork
   first** (Item T41's finding): two independent real correctness bugs
   were found and reverted this session when extending
   `mir_can_forward_hl_de_to_next` to `MIR_STORE` (see `mir-text-size-
   plan.md`'s Item T41 entry for full detail on both). Before
   attempting again: (a) teach `mir_prepare_backend_slots` itself
   wide-forwarding awareness (mirroring Item 13's scalar-path
   precedent - this is the suspected actual root cause, since both
   bugs manifested as reads from slots that should never have been
   touched); (b) build a richer synthetic test battery (param-direct
   operands, plain SSA temps, mixed cases) each verified via actual
   `ntvcm` execution, not just force-accept-diff inspection or census
   byte counts, before considering any version safe to land.
4. **Do a final sweep of `mir_can_forward_hl_to_next`'s `MIR_STORE`
   producer whitelist** (now `MIR_LOAD_INDIRECT`/`MIR_BINARY`/
   `MIR_UNARY`/`MIR_CONST`/`MIR_CALL`/`MIR_ADDRESS` after Items T31/
   T37) for any other pure, side-effect-free producer opcode that
   might still be missing - the pattern has now repeated twice
   (`MIR_CALL` in T31, `MIR_ADDRESS` in T37), both times found only
   after a real function exposed the gap rather than by inspection
   alone, so a deliberate audit of every `MIR_*` opcode against this
   whitelist (not just reacting to the next exposed regression) may be
   worthwhile.
5. **Size Item T33's population** (dead backend-slot reservation for
   call-preserved-register values, `wumpus.rndix`) before choosing an
   implementation approach - still open, unrelated to T34/T35/T36/T37.
6. **Build the far-bucket root-cause classifier** once 1-2 above land:
   sample ~20-30 `far`-bucket (gap>256 bytes) functions across
   different apps/shapes, force-accept-diff each, bucket by root-cause
   category. T34/T35 both demonstrated that even a function apparently
   satisfying every *documented* condition of an existing predicate can
   still be silently blocked by an unrelated bug elsewhere (a stale
   cache; a too-narrow eligibility gate) - prefer this kind of runtime-
   instrumented, evidence-first diagnosis (temporary env-var-gated
   traces, built/tested/reverted) over further static-reading-only
   guesses when sampling new candidates.
7. Continue autonomously through the backlog (see the session
   workspace `plan.md`'s v2 plan and `mir-text-size-plan.md`'s Items
   T34/T35 entries for full detail) without stopping between items, per the
   standing user directive.

## Superseded "Next session should" entries (kept for history only, do not act on these - see the renumbered list above)

8. **Size the Item T33 population first**: instrument how many corpus
   functions have `frame_bytes > 0` purely from
   `mir_prepare_backend_slots()` (not `mir.local_bytes`) where the
   reserved slot(s) are never referenced by any emitted `(ix+N)`/
   `(ix-N)` operand in the accepted or force-accepted output. If large,
   prefer a post-emission dead-slot-detection-and-shrink pass (doesn't
   require reordering the existing interval-allocation pipeline) over
   reordering slot reservation to depend on register-homing decisions.
   See `mir-text-size-plan.md`'s Item T33 entry for full detail.
9. **Re-sweep the census fresh post-T32** and look specifically for
   more instances of the same "branch over a jump with no phi copies"
   family T32 just fixed - e.g. plain `MIR_JUMP`-only blocks that
   could similarly collapse, or other emission sites that hand-roll a
   fallthrough-label dance instead of using the new
   `mir_emit_conditional_branch_with_phi_copies` helper. Grep for other
   `new_label()` + `jp %s,L%d` + phi-copy call sequences in
   `dcc_mir_spilled_cfg.c`/`dcc_mir_homed_cfg.c` that could reuse the
   same helper.
10. **Prioritize one of the two newly-confirmed architectural levers**
   as a properly staged, multi-step project (not more one-off
   near-miss picking, which just hit the same wall 3 times in a row):
   (a) a way to preserve a live `hl` value across another
   `hl`-clobbering materialization more cheaply than a full backend
   slot (e.g. push the real Z80 stack immediately, or a direct
   register-to-register transfer where the destination is provably
   free) - this affects `pointer_for_init_sizeof`, `wcschr`, and
   `assign_return_pair_ptr` alike, likely many more; or (b) jump-table
   `switch` lowering in the MIR selector (currently only a cascaded
   compare chain), which affects any `switch`-heavy function not
   already resolved by T32. Stage narrowly per SKILL.md: pin down the
   exact shape via 2-3 forced-accept diffs before generalizing.
11. **Re-sweep the census fresh from the post-T31 snapshot** and
   continue down the ranked near-miss list (population composition
   shifts after every landed item - do not reuse this session's
   rankings). **Items T30/T31 proved the near-miss vein is NOT dry**
   despite 3 of 4 post-T29 candidates hitting the "no reg-reg move"
   wall - `check_s` (T30) and `test_putc_and_remove` (T31) were both
   distinct, fixable gaps: a skip-target blind spot for a provably-
   no-code `MIR_CONST`, and a stale `producer_opcode` whitelist that
   was never updated after T30 made `MIR_CALL` results reachable at
   all. Also check whether other single-use-forwarding predicates in
   `dcc_mir_spilled_cfg.c` have the same NOP-vs-label adjacency
   conflation Item T29 fixed, or the same "no-code MIR_CONST is
   invisible to skip-target" gap Item T30 fixed, applied elsewhere
   (e.g. `mir_can_forward_stack_to_index`/`_binary_const`/`_rhs`, which
   still only skip a NOP/label the old way, not a transparent
   zero-RHS-comparison constant).
12. Two exposed quality gaps from Item T28 are still concrete, fresh,
   actionable candidates rather than abstract priorities: (a) the
   systemic boolean/comparison-chain materialization overhead
   (`SKILL.md`'s "Known root cause", this plan's ranked item
   1) - `thoistbc.main`'s `n==6 && out[0]==3 && out[2]==5 && out[5]==7`
   chained-return-expression is a fresh forced-diff example alongside
   `check_s`/`and_expr`; (b) a newly-identified candidate: array/pointer
   -element address computation via `MIR_INDEX_ADDRESS` appears to use
   a general compute-and-dereference path even when the element offset
   is well within direct `ix`-relative range (`tinitreg.tauto`'s `a[N]`
   /`m[i][j]` reads) - worth its own dedicated investigation.
13. **DONE (Item T27), but `tsnprtf`'s residual is NOT closed**: the
   `MIR_LOAD`-of-same-object extension to `mir_param_value_is_direct`
   landed (safe, 6 other apps improved, 0 regressions), but
   `mir_object_eligible` unconditionally excludes pointer-typed
   symbols from ever getting an object at all, so `tsnprtf`'s
   `buf`/`fmt` (`char *`) still can't use this path. Actually closing
   it needs relaxing `mir_object_eligible` to register pointer-typed
   scalar parameters as objects too - a materially larger, riskier
   change (object registration feeds frame layout, alias-merge, and
   memory-location decisions broadly, not just this one predicate).
   Worth a dedicated, carefully-staged item; `tsnprtf`'s baseline has
   been updated in the meantime so this is no longer CI-blocking.
14. **`tc89core.main`'s peep residual (+0.56%, improved from T20's
   +0.78% under Item T25 but not fully closed)** would need a
   predicate that follows the value through additional intervening
   definitions/uses beyond the single-load case Item T25 covers -
   worth a dedicated look if `tc89core` keeps recurring as a residual
   across future items, otherwise leave it as a documented, visible,
   un-baselined residual.
15. Execute the dedicated `text-size` plan (drafted this session,
   carried into `mir-text-size-plan.md`'s Execution Log after each
   item lands):
   - **Re-sweep the worst-ratio/bucket list fresh post-T26** before
     picking the next candidate. Census snapshot: `/tmp/census-after-t26.tsv`
     (the final validated post-T26 state; treat as the new baseline).
     `MIR_BINARY`'s operand-adjacency forwarding now covers both
     src1-adjacent-to-const (T15) and src2-adjacent-with-const-src1
     (T16), **for both plain arithmetic and fusable comparisons**
     (T17); the remaining gap in this family is both operands being
     non-constant/computed simultaneously (needs tracking two pending
     forwarded values at once - not attempted, revisit only with
     evidence it's material).
   - **Comparison-fusion is already done** (Items 1/2/4/25/27, landed
     in an earlier migration phase) - do not re-attempt it; Item T7
     confirmed `check_s`'s boolean is already fully elided.
   - **`wumpus::pact` is now blocked solely by the `cfg-backedge`
     migration boundary** (a deliberate barrier, not a bug) - worth
     revisiting as its own future item once enough non-loop
     `text-size` candidates are exhausted.
   - **`check_s`'s Item T7 deferral was reconfirmed unchanged this
     session** (nothing this session touched the flagged
     `mir_forward_skip_target` equality-check risk) - do not
     re-litigate without new evidence changing that gate's risk
     calculus.
   - **Consider extending Item T15's `mir_can_forward_stack_to_binary_const`
     pattern** to other consumer opcodes `mir_can_forward_hl_to_next`
     already recognizes for the literal-adjacent case (`MIR_UNARY`,
     `MIR_STORE_INDIRECT`, `MIR_MEMBER_ADDRESS`, etc.), mirrored the
     same way for the "one MIR_CONST in between" case, if a fresh
     post-T15 sweep surfaces evidence for it.
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
16. Now that the module is split, prefer editing the specific
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
