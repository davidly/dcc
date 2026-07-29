# MIR Emitter Migration Plan: Next 50

## Checkpoint

Baseline census: `build/mir-plan-50-before.tsv`, 161 of 2319 functions
accepted (6.94%). The fallback corpus is dominated by `text-size` (2113
functions); semantic gates remain deliberately closed until their lowering is
correct and target-profitable.

Each item is a structural migration investigation, not permission to accept a
named function. A completed item requires a before/after census, a forced
candidate A/B where applicable, and `runall.ps1 -Mode full` for every newly
emitted app. Never update performance baselines during this work.

| # | Emitter migration | Initial discriminator / target |
|---|---|---|
| 1 | Reuse a dead scalar binary operand's virtual slot for its result | `tdmfuse.test_first_stmt_reassigns_operand` |
| 2 | Reuse a dead scalar unary operand's virtual slot for its result | `tunary` conversion helpers |
| 3 | Reuse a dead wide binary operand's slot for its result | `tlong` identity helpers |
| 4 | Reuse a dead wide unary operand's slot for its result | `tc89flng.idf` |
| 5 | Reuse a dead compare operand slot for a subsequent materialized boolean | `tbool.bool_identity` |
| 6 | Forward binary results directly to following stores | scalar assignment helpers |
| 7 | Forward unary results directly to following stores | unary promotion helpers |
| 8 | Forward division/modulo results directly to following stores | `tmuldiv.i16_test` |
| 9 | Forward result registers directly to returns | single-block scalar return helpers |
| 10 | Coalesce final return slots with dead arithmetic operands | `tc89size.nb_between_locals` |
| 11 | Eliminate dead promoted-local initialization stores | promoted local scalar tests |
| 12 | Eliminate overwritten promoted-local initialization stores | `tdmfuse` reassignment cases |
| 13 | Fold constant binary operations before slot assignment | constant arithmetic fixtures |
| 14 | Fold constant comparison operations before slot assignment | boolean comparison fixtures |
| 15 | Fold constant shifts before slot assignment | `ts.sh*`, `tunary.sh*` |
| 16 | Materialize constant divisors directly without a slot | `tc89size.nb_*` remainder |
| 17 | Materialize constant dividend directly without a slot | `tc89size.nb_shadow_outer_after` |
| 18 | Improve constant address-plus-offset materialization | string and array-address call sites |
| 19 | Rematerialize repeated short string addresses at calls | assertion/check helpers |
| 20 | Rematerialize repeated function addresses at indirect calls | `tc99apar.call_callback` |
| 21 | Cache a two-argument call value across adjacent arguments | formatted-I/O wrapper candidates |
| 22 | Cache a three-argument call value across adjacent arguments | non-variadic check helpers |
| 23 | Avoid storing unused scalar call results | void-like helper call sites |
| 24 | Avoid storing unused wide scalar call results | long helper call sites |
| 25 | Avoid redundant callee-result moves before an immediate compare | comparison-wrapper helpers |
| 26 | Select direct zero-comparison branches | `tesc.check_s`-style conditions |
| 27 | Select direct nonzero-comparison branches | boolean helper conditions |
| 28 | Select direct signed relational branches | scalar comparison tests |
| 29 | Select direct unsigned relational branches | unsigned comparison tests |
| 30 | Remove redundant compare-result materialization | branch-only comparison MIR |
| 31 | Reuse branch fall-through labels without unconditional jumps | two-block scalar CFGs |
| 32 | Elide empty branch-edge PHI copies | small conditional expressions |
| 33 | Coalesce same-register PHI copies | scalar conditional helpers |
| 34 | Add return-value PHI forwarding | multi-return scalar functions |
| 35 | Hoist shared call-argument constants within one block | repeated check calls |
| 36 | Elide duplicate string-address loads in adjacent calls | test assertion helpers |
| 37 | Lower 16-bit multiply by small constants without runtime call | arithmetic regression tests |
| 38 | Lower signed divide/mod by powers of two with C-correct rounding | focused arithmetic fixtures |
| 39 | Lower unsigned divide/mod by powers of two | unsigned arithmetic fixtures |
| 40 | Fuse adjacent safe scalar div/mod pairs | existing `tdmfuse` positive fixtures |
| 41 | Preserve reassignment/alias decline rules for div/mod fusion | `tdmfuse` negative fixtures |
| 42 | Improve 32-bit call-argument register preservation | long formatting/check helpers |
| 43 | Lower 32-bit equality/inequality without stack round trips | `tlong` identity helpers |
| 44 | Lower 32-bit constant comparisons directly | long comparison helpers |
| 45 | Lower 32-bit zero tests directly to branches | long boolean helpers |
| 46 | Reduce wide temporary frame slots across a call | `tc89f*` identity wrappers |
| 47 | Improve float identity/conversion temporary handling | `tc89fadd`, `tc89fcmp`, `tfloat4` |
| 48 | Add dense-switch jump-table lowering before changing switch cost policy | C89 switch fixtures |
| 49 | Repair inline-substitution lowering before changing its semantic gate | `tinline` focused fixtures |
| 50 | Profile and lower bounded loop/pointer-array classes before changing gates | `tchess.is_attacked`, pointer-array fixtures |

## Execution Order

Items 1-25 are slot, forwarding, and rematerialization work and may admit
near-cost single-block functions transactionally. Items 26-36 require CFG
selection evidence. Items 37-47 require a focused target benchmark for every
new instruction form. Items 48-50 are architectural work; their current gates
must remain in place until the stated lowering exists and passes full-mode
validation.

## Next Batch (20 items)

Completed so far: 1, 2, 3, 4, 9, 10, 37. This batch picks the next 20 items,
ordered for maximum reuse of already-validated infrastructure before moving to
higher-risk CFG/call work. Same completion bar as above applies to each.

| Order | # | Emitter migration | Why next |
|---|---|---|---|
| 1 | 5 | Reuse a dead compare operand slot for a subsequent materialized boolean | Completes the Item 1-4 slot-reuse family with the one remaining operand class |
| 2 | 6 | Forward binary results directly to following stores | Same forwarding mechanism validated by Item 9/10, applied to stores instead of returns |
| 3 | 7 | Forward unary results directly to following stores | Pairs directly with Item 6 |
| 4 | 8 | Forward division/modulo results directly to following stores | Completes the forward-to-store family; division results are the highest-cost case to avoid re-spilling |
| 5 | 11 | Eliminate dead promoted-local initialization stores | Low-risk dead-store elimination, no new instruction forms |
| 6 | 12 | Eliminate overwritten promoted-local initialization stores | Pairs directly with Item 11 |
| 7 | 13 | Fold constant binary operations before slot assignment | Removes slot pressure before it exists rather than reusing after the fact |
| 8 | 14 | Fold constant comparison operations before slot assignment | Pairs directly with Item 13 |
| 9 | 15 | Fold constant shifts before slot assignment | Completes the constant-fold family |
| 10 | 16 | Materialize constant divisors directly without a slot | Immediate follow-on to Item 37 in the same arithmetic-lowering area |
| 11 | 17 | Materialize constant dividend directly without a slot | Pairs directly with Item 16 |
| 12 | 38 | Lower signed divide/mod by powers of two with C-correct rounding | Continues the constant-arithmetic strength reduction started by Item 37 while the `dcc_ops.c` reference algorithms are fresh context |
| 13 | 39 | Lower unsigned divide/mod by powers of two | Pairs directly with Item 38 |
| 14 | 18 | Improve constant address-plus-offset materialization | Enables Items 19/20 below |
| 15 | 19 | Rematerialize repeated short string addresses at calls | Depends on Item 18 |
| 16 | 20 | Rematerialize repeated function addresses at indirect calls | Depends on Item 18 |
| 17 | 23 | Avoid storing unused scalar call results | Independent, low-risk dead-store elimination at call sites |
| 18 | 24 | Avoid storing unused wide scalar call results | Pairs directly with Item 23 |
| 19 | 21 | Cache a two-argument call value across adjacent arguments | Call-argument caching; higher risk (aliasing) so scheduled after the dead-store items |
| 20 | 22 | Cache a three-argument call value across adjacent arguments | Pairs directly with Item 21 |

Deferred to the following batch: Items 26-36 (CFG/branch/PHI selection,
needs evidence from a broader set of two-block functions), Items 40-50
(div/mod fusion, 32-bit lowering, float, switch, inline-substitution, and
the architectural loop/pointer-array item), each of which either depends on
CFG selection groundwork not yet built or needs its own dedicated benchmark.

## Current Execution Log

- Item 1 completed: scalar `MIR_BINARY` results reuse a dead 16-bit operand's
  virtual slot. The change reduced the `tdmfuse` candidate by five assembly
  bytes and passed the full target suite for every changed active-MIR app:
  `tc89comp`, `tdmfuse`, and `tvla`. It admitted no new function yet, so the
  next item remains a separate transactional migration.

- Item 9 attempted and initially reverted, then re-attempted with a
  narrower discriminator (see below). The first attempt broadened
  `mir_can_forward_hl_to_next`/`mir_emit_virtual_store` to forward a
  scalar result across intervening `MIR_NOP`s directly into an immediately
  following `MIR_RETURN`, without any call/VLA restriction. Corpus census
  showed 9 newly admitted functions across `tc89size`, `tenumfsm`, and
  `tvla`; full-mode validation found real regressions in `tenumfsm` (peep
  cycles +0.08%) and `tvla` (nopeep cycles +0.01%, nopeep bytes +1.59%). A
  follow-up mitigation gating on "any `MIR_VLA_SIZE` in the function" was
  found to be too broad (it would have dropped 9 previously-valid VLA
  sizeof functions back to fallback) and was also reverted. Root-cause
  analysis via `DCC_MIR_REPORT`/`DCC_MIR_FUNCTION` diagnostics found
  `mir_cfg_block_count() == 1` is not a valid discriminator (both the safe
  `tbool.bool_identity`/`tc89size.nb_between_locals` candidates and the
  regressing `tenumfsm.main` are single-block). The real distinguishing
  factor was the presence of a `MIR_CALL`/`MIR_CALL_AGGREGATE` earlier in
  the function (`tenumfsm.main` has `cross-call=1` from `scanf`/`printf`;
  the safe candidates have none) plus VLA usage (`tvla`'s regressing
  functions all have `mir.has_vla`).

- Item 9/10 completed (re-attempt): restricted return-forwarding to
  call-free, non-VLA functions only (`mir_function_has_any_call()` plus
  the existing `mir.has_vla` check, gating specifically the
  `next->opcode == MIR_RETURN` branch). Full corpus census against the
  Item-1 baseline shows exactly one newly admitted function,
  `tc89size.nb_between_locals` (Item 10's target), with `tenumfsm.main`
  and all `tvla.vla_sizeof_*` functions correctly remaining excluded
  (verified: `tenumfsm.main` reports `fallback text-size`; `tvla` still
  reports exactly the same 7 previously-accepted `vla_sizeof_*`
  functions as `mir accepted`, no more, no less). `tbool.bool_identity`
  improved from 222 to 170 generated bytes (captured 142) but remains
  just outside the near-cost fallback threshold, so it stays on
  fallback — correctly, since it is not yet profitable enough to accept.
  `runall.ps1 -Apps tc89size -Mode full` passed with zero regressions and
  three improvements: peep cycles -0.32%, nopeep cycles -1%, nopeep bytes
  -1.05%. New coverage: 162/2319 (6.99%). New baseline snapshot:
  `build/mir-plan-50-item-10.tsv`.

- Item 2 completed: extended the Item 1 dead-operand slot reuse to
  `MIR_UNARY` results (16-bit scalar unary ops reuse their dying 16-bit
  operand's slot, guarded by `!mir_definition_is_wide` on the source).
  Census against the Item 10 baseline showed zero newly-admitted
  functions but 71 apps with smaller generated candidate sizes for
  currently-fallback functions (no runtime-validated app changed, so no
  `runall.ps1` run was required). New baseline: `build/mir-plan-50-item-2.tsv`
  (promoted to `build/mir-plan-50-baseline.tsv`).

- Items 3/4 completed: generalized the reuse check to 32-bit (`units == 2`)
  operands for both `MIR_BINARY` and `MIR_UNARY`, matching wide
  source/destination unit counts and fixing the reuse path to mark
  `slot_end` for every reused unit (previously only unit 0 was marked,
  which would have been a latent bug once wide reuse was enabled). Census
  against the Item 2 baseline showed zero newly-admitted functions and 70
  apps with smaller fallback-candidate sizes; zero apps required runtime
  validation. New baseline: `build/mir-plan-50-item-34.tsv` (promoted to
  `build/mir-plan-50-baseline.tsv`).

- Item 5 completed: generalized the Item 1-4 dead-operand slot-reuse check in
  `mir_prepare_backend_slots` to match against operand width
  (`operand_units`, derived from `secondary_offset`) instead of result
  width, adding the one previously-missing case: a narrow (16-bit) boolean
  result from a wide (32-bit) comparison (`<`, `>`, `==`, etc. on `long`
  operands) now reuses the first unit of a dying wide operand's two-unit
  slot, freeing the second unit for reuse elsewhere. The prior two cases
  (narrow-from-narrow, wide-from-wide) are unaffected — `operand_units`
  reduces to the same value as `units` for both. Census against the
  Item-37 baseline showed 0 newly-admitted functions (as expected: this
  only shrinks already-fallback candidate sizes) but 84 apps with smaller
  generated-candidate sizes, 4 of which (`tcrcfix`, `tscanf`, `tstdlib`,
  `tsyntax`) required runtime validation for already-accepted functions
  whose byte counts changed. `runall.ps1 -Apps tcrcfix,tscanf,tstdlib,tsyntax
  -Mode full` passed with zero regressions, and `runall.ps1 -Mode full
  -Extended` passed corpus-wide (319 apps, 0 failures). New baseline:
  promoted directly to `build/mir-plan-50-baseline.tsv`.

- Items 6/7/8 completed together (binary/unary/divmod results forwarded
  directly to a following store share one mechanism, since divmod is
  emitted as `MIR_BINARY`). Investigation found the `MIR_STORE` case in
  `mir_can_forward_hl_to_next`'s switch was unreachable dead code: the
  function's leading gate collapsed to "only `MIR_RETURN` is ever a valid
  forwarding target" because it required `mir_virtual_iy_base` for any
  other next-opcode, and `mir_virtual_iy_base` is initialized to 0 and
  never set true anywhere in the file (reserved scaffolding for an
  IY-relative virtual frame base that was never implemented). Fixed by
  (1) broadening the `MIR_STORE` case's producer check from
  `MIR_LOAD_INDIRECT`-only to also accept `MIR_BINARY`/`MIR_UNARY`
  producers, and (2) adding an explicit `next->opcode != MIR_STORE`
  exception to the leading gate so the adjacent (no intervening `MIR_NOP`)
  store case is reachable without requiring the dead `mir_virtual_iy_base`
  flag. Left the `mir_virtual_iy_base`-gated `MEMBER_ADDRESS`/
  `INDEX_ADDRESS`/`STORE_INDIRECT`-as-next cases untouched (out of scope
  for this batch; each needs its own targeted investigation). Census
  against the Item-5 baseline showed 0 newly-admitted functions but 204
  apps with smaller fallback-candidate sizes, 0 of which required runtime
  validation (no currently-accepted function's generated size changed).
  `runall.ps1 -Mode full -Extended` passed corpus-wide (319 apps, 0
  failures) as an extra safety check given the breadth of previously-dead
  code now reachable. New baseline: promoted directly to
  `build/mir-plan-50-baseline.tsv`.

- Item 37 completed: general 16-bit multiply-by-constant strength reduction,
  ported from the legacy `dcc_ops.c` reference algorithm
  (`emit_mul_hl_const`/`mul_const_op_count`/`emit_mul_hl_const_general`,
  `MUL_CONST_MAX_OPS 10`) into `dcc_mir.c` as
  `mir_emit_mul_hl_const`/`mir_mul_const_op_count`/
  `mir_emit_mul_hl_const_general`, guarded by a broadened
  `mir_multiply_by_small_constant` predicate (previously power-of-two only).
  This was motivated by a user request to genuinely fix VLA sizeof-multiply
  regressions rather than exclude them: the prior blanket `mir.has_vla`
  exclusion in `mir_can_forward_hl_to_next`'s return-forwarding gate (Item
  9/10) was masking a real, corpus-wide bug — any non-power-of-two constant
  multiply fell through to a generic `__mulu` runtime call, which is both
  bigger and slower than the shift/add sequence the legacy backend already
  emits. Direct assembly diff of `tvla.vla_sizeof_2d_row` (forced-fallback
  legacy vs. normal MIR) confirmed this: MIR emitted `extrn __mulu`/`call
  __mulu` for `rows*6` where legacy emitted a shift/add sequence.
  The `mir.has_vla` exclusion in `mir_can_forward_hl_to_next` remains
  unchanged (still required for the RETURN-forwarding mechanism
  specifically); the multiply fix is an orthogonal, independent mechanism
  that achieves genuine new VLA admissions through the byte-size cost gate.
  Census against the Item-3/4 baseline initially showed +3 admissions
  (`tvla.vla_sizeof_2d_row`, `tvla.vla_sizeof_2d_rows`,
  `tvla.vla_sizeof_op_mulrhs`), but `runall.ps1 -Apps tvla -Mode full` found
  a tiny nopeep-only regression (+67 cycles, ~0.0002%) traced via
  `DCC_MIR_FORCE_FALLBACK_FUNCTION` bisection to `vla_sizeof_2d_rows` alone.
  That function uniquely combines a general (non-power-of-two)
  constant-multiply that sizes a VLA allocation with a later integer
  division elsewhere in the same function; the resulting VLA-frame
  store/reload traffic is undercounted by the static byte/instruction cost
  gate (cheap in bytes, not free in cycles), and only `dccpeep` currently
  cleans it up. A first attempt at a fix (blanket-excluding any function
  combining `mir.has_vla` with a `/` or `%` operator) was too broad — it
  regressed 4 already-safely-accepted baseline functions
  (`vla_sizeof_count`, `vla_sizeof_deep_nested`, `vla_sizeof_nested_block`,
  `vla_sizeof_shadow_inner`) that already contain division unrelated to
  this change. The correct, narrow discriminator (verified via MIR dumps of
  both `vla_sizeof_2d_row`, which has the same alloc-sizing multiply but no
  division and stays safely admitted, and `vla_sizeof_2d_rows`) is: restrict
  the new general (non-power-of-two) strength reduction to skip only when
  its result both feeds a `MIR_VLA_ALLOC` size operand *and* the function
  also contains a later `/` or `%` (`mir_value_feeds_vla_alloc` +
  `mir_has_integer_division`, combined). Pre-existing power-of-two handling
  (used unconditionally by e.g. `vla_sizeof_count`'s `rows*2`) is left
  untouched by this guard. Final census against the Item-3/4 baseline: +2
  admissions (`tvla.vla_sizeof_2d_row`, `tvla.vla_sizeof_op_mulrhs`), 0
  losses; coverage 164/2319 (7.07%). `runall.ps1 -Apps tvla -Mode full`
  passed with zero regressions and three improvements (peep cycles -0.01%,
  peep bytes -0.43%, nopeep cycles -0%). Full corpus validation via
  `runall.ps1 -Mode full -Extended` also passed (319 apps, 310 runnable, 196
  extended, 0 failures). New baseline: promoted directly to
  `build/mir-plan-50-baseline.tsv`.