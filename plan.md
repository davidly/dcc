# dcc MIR migration: coverage-first sprint to 100%

`mir-text-size-plan.md` is the authoritative experiment log. This file is the
current execution plan and handoff.

## 2026-08-12 current checkpoint (read this first)

- Branch: `pr/143`, published to `origin/perf/unified-regalloc`.
- Published HEAD before this batch: `0fbf86d`
  (`prove local boolean checks`).
- Current candidate coverage: **2026/2185 (92.72%)**.
- Remaining fallback population: **159 `final-cost-policy`**, all selected by
  the spilled scalar backend and with no other fallback reason.
- `a1` is **23/23 MIR**. Relative to main it is **11.12% faster peep** and
  **12.98% faster nopeep**, with no checked size regression.
- The release gate is clean: 314 runnable apps, diagnostics, 22 dccpeep
  fixtures, and the extended suite pass in both peep/nopeep modes.
- Large-CFG MIR now uses depth-three LIFO stack handoffs, typed compact byte
  slots, call-bounded store-address forwarding, single-use unary forwarding,
  and direct `+1/+2` increments. Tagged byte-slot cleanup and final liveness
  narrowing in dccpeep preserve larger canonical passes.
- The scheduled byte-array reduction keeps the pointer in BC, the accumulator
  in DE, and the endpoint in callee-saved IY. `treg.sumarray` is newly MIR and
  improves **7.33% peep / 8.65% nopeep**.
- The fixed-three-column word reduction flattens `tvla.vla_sum2d` to one
  endpoint loop. It is newly MIR and improves both modes while reducing linked
  size by 256/384 bytes.
- The structurally profiled VLA wide-truncation loop admits
  `tvla.vla_long_rhs_store` after forced dual-mode A/B proved it faster and
  smaller; the predicate is terminal, hashless, and matches one candidate.
- The structurally profiled variadic macro-validation loop admits
  `tvariad.check_macro_values`; forced and production full-mode runs improve
  about 5% in both modes and reduce linked size.
- The scheduled fixed-wide zero scan admits hot `catalan.is_zero`, improving
  both modes and shrinking 128 bytes. The profiled call-check runner predicate
  admits both `tstretst.run_direct` and `run_helper`; together they improve
  0.66% peep / 4.87% nopeep.
- The constant byte-fill scheduler admits `tecreg.fill_bytes`, using HL/A/B
  instead of a frame induction slot. It improves 5.74% peep / 28.07% nopeep.
- The local fill+sum+print scheduler admits `tecreg.main`, completing that app's
  MIR coverage and improving it 43.37% peep / 59.46% nopeep.
- The affine byte-fill scheduler keeps the pointer in HL and fill byte in A,
  deriving each stored byte from B+C without frame traffic. Its companion
  local reduction scheduler admits `tctrreg.stamp` and `tctrreg.main`,
  completing that app's MIR coverage and improving it 24.68% peep / 40.49%
  nopeep.
- The terminal constant-switch scheduler uses frameless SP-relative parameter
  access and word result tables. A bounded constant-flow evaluator folds pure
  local fallthrough updates while rejecting parameter-dependent tail control.
  It admits `tc89swjt.swdn`, `tc89swjt.swft`, and `tdead.ds_sw`;
  `tc89swjt` improves 7.87% peep / 9.42% nopeep.
- The wide left-shift counter keeps the unsigned long in DE:HL and the loop
  count in BC. It admits `tcrcfix.crc_t_bits_probe`, improving that app 7.53%
  peep / 6.70% nopeep and shrinking both linked modes.
- The palindrome scheduler scans through HL, then keeps the converging left
  and right pointers in BC and DE. It admits `tforfrm.palindrome`, beating the
  10% stretch goal at 12.35% peep / 18.80% nopeep.
- The dynamic-row scheduler keeps the evolving row in DE and the byte column
  offset in BC, rematerializing the table base only after the old row dies.
  It admits `trowptr.memory_target`, improving 4.88% peep / 5.14% nopeep.
- The bounded constant-loop check proves the induction reduction and final
  predicate before preserving only the observable check call. It admits
  `treg.test_register_int`, bringing `treg` to 9.91% peep / 12.10% nopeep
  faster than main.
- The global byte countdown collapses the modulo-256 induction count and six
  stable global loads into one register expression. It admits
  `tbcgcol.global_bc_across_byte_loop`, improving both modes.
- The bounded constant-function evaluator executes side-effect-free FINAL MIR
  with PHIs under target integer semantics and emits only the proven result.
  It admits `tnarwin.sumten` plus `tregnarw.lres/lmod/lbig`; the latter app
  improves 70.01% peep / 73.32% nopeep.
- The conditional string reporter retains the name pointer in BC, selects the
  result string once, and pushes printf arguments directly. It admits
  `tstr2.report_test` and improves both modes.
- The affine byte-fill plan now supports a constant initial A and fixed byte
  step as well as a parameter base. It admits `tptrixld.fill`, improving
  21.13% peep / 25.06% nopeep.
- Frameless signed-word range and ASCII uppercase helpers admit
  `tchess.on_board` and `tchess.upiece`. The forced-accept batch tool now
  exercises the terminal cost override rather than the obsolete earlier gate.
- The fixed word-array reduction preserves adjusted pointer qualifiers:
  stable pointers stay resident in BC, while volatile pointer objects are
  reloaded once per source access. It admits `tc99apar.sum_const` and
  `sum_volatile`, improving 6.53% peep / 7.91% nopeep.
- The by-value slice reduction derives aggregate member offsets, keeps its
  cursor in BC, accumulator in DE, and endpoint in IY. It admits
  `tc89comp.slice_sum`, improving both modes.
- Constant result tables now support up to 64 entries. This admits the
  35-case `tswitch.f`, beating the stretch goal at 10.41% peep / 11.28%
  nopeep.
- The bounded constant evaluator now tracks proven nonvolatile, non-aliased
  local object state and target-width bitwise operations. It admits
  `tbug.swbr/swfc/swwc`; `tbug` improves 30.79% peep / 27.55% nopeep.
- Compact conditional pointer identity, wide constant equality, and float
  truth schedules admit `tctxflt.ad_castptr/cfk_case/truth_while`, improving
  both modes and shrinking both linked images.
- Nested conditional word selection eliminates lossless int/float/long
  round trips for `tctxflt.cond_nested` and `cond_ncast`, improving the app
  to 2.59% peep / 2.75% nopeep faster than main.
- Direct float/int truth combinators preserve short-circuit AND/OR semantics
  without runtime helpers. They admit `tctxflt.truth_and/truth_or` and further
  reduce both modes and linked sizes.
- Conditional float-to-long emission keeps the constant arm direct and invokes
  `__faf` or a validated float-returning callee only on the false arm. It
  admits `tctxflt.cond_compound/cond_callarm`.
- Conditional global-pointer, nested member, and float-comparison schedules
  complete `tctxflt` MIR coverage. The app is 3.40% peep / 3.65% nopeep
  faster than main and both linked images are smaller.
- Conditional integer selection followed by `_Bool` normalization now emits
  as a direct truth test, admitting `tbool.ternary_bool` and improving both
  modes.
- Cleared record append keeps the record pointer in callee-saved IY across
  `memset`/`strcpy`, uses frameless SP-relative parameters, and rematerializes
  the return index. It admits `tstfield.add_word` and improves both modes.
- Backward record-name search keeps the descending index in IY across
  `strcmp` and reloads the stable name parameter only at the call boundary.
  It admits `tstfield.find_word`; that app is now 1.02% peep / 2.41% nopeep
  faster than main.
- Conditional integer selection now normalizes to the declared target width,
  admitting `ts.bc`; `ts` improves 0.44% peep / 0.48% nopeep and shrinks.
- Sequential unary reports evaluate helper calls in ABI argument order and
  push each result immediately, eliminating eight spills in `ts.shbool`.
  `ts` improves 0.50% peep / 0.59% nopeep.
- Nibble append keeps the destination in HL and classifies the value in A,
  admitting `tarray.aHexNibble`; `tarray` improves 3.05% peep / 5.04% nopeep.
- Constant IEEE float checks now prove an observable failure block unreachable,
  admitting `tc89c2.test_huge_val`; `tc89c2` improves 4.55% peep / 5.33%
  nopeep and shrinks further.
- Volatile local fill plus constant wide-shift proof preserves all 160 required
  stores while eliminating dead wide frame traffic. It admits
  `tcrcfix.non_ix_shift_store_probe`; `tcrcfix` improves 36.49% peep / 38.00%
  nopeep.
- Signed div/mod check wrappers now emit only the result that reaches `ck`,
  eliminating each dead companion operation. This admits
  `tdmfuse.sdm_pair/sdm_pair_r`, improving 4.29% peep / 4.76% nopeep.
- Escaped local identity arrays preserve the published stack address but fold
  the known returned element, admitting `tnarrow.narwesc`; `tnarrow` improves
  2.11% peep / 2.45% nopeep.
- Wraparound boolean-neighbor loops now keep the current and next pointers,
  index, and wide live count in IY, DE, BC, and shadow BC. Replacing two signed
  modulo helpers per iteration admits `tptrarr.step` and improves the app
  83.76% peep / 85.30% nopeep (83.72% / 85.29% faster than main).
- Reduced sine/cosine polynomial kernels now preserve the established FMA
  chains while using one compact x2 slot; cosine tracks quadrant sign in one
  byte and applies it without a final float multiply. This admits
  `ttrig.sinf/cosf` with a small 0.05% dual-mode app gain; float runtime
  helpers remain the dominant stretch ceiling.
- Tangent rational kernels now retain their reduced argument and x2 in one
  compact frame, keep the numerator on the evaluation stack, directly test
  IEEE zero, and materialize a reciprocal only for inverted quadrants. This
  admits `ttrig.tanf`; the combined `ttrig` gain is 0.08% in both modes.
- Compact scalar schedules now cover word-width ASCII case mapping and
  short-circuit logical OR parameters, admitting `tchess.xtolower` and
  `tinline.edge_or`. A registerized fixed-count byte mismatch scan keeps the
  pointer, expected byte, endpoint, and index derivation in registers,
  admitting `tctresc.find_mismatch` and improving that app 1.61% peep /
  14.01% nopeep.
- Float-tolerance failure checks now keep the subtraction result in DE:HL,
  normalize its sign without a frame spill, and branch directly to the
  string-only failure report. This admits `tclit.check_float`, improving
  `tclit` 2.87% peep / 2.89% nopeep and moving it ahead of main.
- Global byte-check sequences now resolve direct, indexed, and member byte
  loads to exact symbols/offsets and push check arguments without virtual
  homes. This admits `tbool.check_globals`, improving `tbool` 0.64% peep /
  0.75% nopeep and removing the final homed-backend fallback.
- Variable-step byte reductions now hold the narrowed induction value in B,
  its step in C, and the word accumulator in DE. The alias-aware form proves
  the local pointer cannot escape and folds its conditional extra update.
  This admits `tpeepal.byte_loop_cache/byte_loop_alias`, improving the app
  1.38% peep / 1.83% nopeep.
- Recursive binary-tree sums now keep wide partial sums on the evaluation
  stack across child calls and inline carry-preserving long additions. Both
  native-wide and sign-extended word members are supported, admitting
  `tclit.sum_tree` and `too.bst_inorder_sum`; `tclit` improves 4.20%/5.24%
  and `too` 1.84%/2.11%.
- Recursive wide linear recurrences now share the product scheduler and select
  either the long multiply helper or an inline carry-preserving addition.
  This admits `triangle.triangle`, improving 23.56% peep / 24.54% nopeep.
- Fixed attention sample kernels now reverse-copy word arrays with IY and fill
  a word array from a no-argument producer while retaining the destination
  pointer across calls. This admits `attnc11.make_targets/generate_sample`
  and reduces the linked nopeep image by 128 bytes.
- Fixed global byte copies with constant scalar-state tails now use LDIR and
  direct byte/word stores. This admits `tchess.init_board`, replacing its
  loop and frame traffic with 13 machine instructions.
- Fixed-stride global call loops now keep the induction value in IY and derive
  each pointer argument directly from its global base and byte stride. This
  admits `attnc11.project_logits` with a dual-mode cycle reduction.
- Compact call-sum reports now preserve intermediate results on the evaluation
  stack across one- and zero-argument calls, then pass the final sum directly
  to the report call. This admits `tasmcoll.main` with a dual-mode gain.
- Dynamic global-array FMA updates now keep the selected element address in IY,
  feed the three float operands directly to `__fmaf`, store the result once,
  and return it without a reload. This admits `tfmadd.array_case`.
- Wide union-bitcast call wrappers now pass float payloads directly through
  the long call ABI and return the resulting bits without local aggregates.
  This admits `tfdf.fdf`, improving 10.37% peep / 10.52% nopeep.
- Wide shift comparisons now add a sign-extended word to a long on the
  evaluation stack, perform constant arithmetic shifts in registers, and use
  a biased inline signed threshold comparison. This admits `tctxops.sh_cmp`.
- Conditional wide additions now choose the wide parameter before evaluation,
  sign-extend the shared word operand, and perform the long addition directly
  on the evaluation stack. This admits `tctxops.ca_tern`.
- Terminal two-case wide switches now evaluate the shared word-plus-long
  expression once and compare DE:HL bytewise without spills. This admits
  `tctxops.ca_switch`.
- Bounded pointer-member appends now keep the aggregate base in IY, update the
  count in BC, and address the pointer array directly. This admits
  `too.world_add`.
- Fixed prediction-count loops now keep the row index in IY, reuse one IX
  result slot across helper calls, and update the global hit/total counters
  directly. This admits `attnc11.count_predictions`.
- The fixed prediction loop now also supports a prefix call and persistent
  boolean result byte, admitting `attnc11.check_sequence`.
- Random wide fills now retain the destination pointer in IY, derive one
  endpoint, and construct each signed Q16 result directly from the producer's
  low byte. This admits `attnc11.initialize_weight_group`.
- Fixed byte-board setup now clears the global board through a register
  endpoint loop, stores the selected byte directly, and emits the ordinary
  four-word call ABI without a frame. This admits `ttt.FindSolution`.
  dccpeep recognizes that MIR call shape and coordinates it with its existing
  whole-file MinMax packed-byte ABI rewrite; `ttt` improves slightly in both
  peep and nopeep modes rather than losing the packed-call optimization.
- Recursive fixed-frame word fills now preserve the source array as a compact
  16-byte IX frame while keeping its pointer in HL, fill value in DE, and
  count in B. Indexed publication and the recursive recurrence are emitted
  directly. This admits `tstackov.descend` and
  `tpragstk.guarded_descend`, improving them 52.03%/57.36% and
  49.11%/54.43% in peep/nopeep modes respectively while preserving the stack
  guard's observable overflow.
- Two-element pointer-member membership checks now keep the searched word in
  BC, load each aggregate pointer directly from its frameless parameter, and
  compare the two adjacent word members without PHI materialization. This
  admits `wumpus.hpit` and `wumpus.hbat` with no app-level regression.
- Wide bitcast call scheduling now handles three float parameters as well as
  two: it pushes their raw bits directly in reverse ABI order and returns the
  wide result without materializing four local unions. This admits
  `tfmaddr.fmaddr` plus `tfmaf.fmadd_/fmaf_`; both apps beat the stretch goal
  at 10.62%/10.73% and 13.17%/13.29% faster in peep/nopeep modes.
- Inline float-tolerance reports now keep the subtraction in DE:HL, normalize
  its magnitude without a local float slot, compare the fixed epsilon
  directly, and push the original values only on failure. This admits
  `tpromo.ck_f` and `tctxops.chkf`, improving those apps 3.01%/3.09% and
  3.55%/3.75% in peep/nopeep modes.
- Global record-pop loops now reload the cheap global state root at each
  boundary, keep the decremented index in BC, form the six-byte record address
  directly, and return immediately on the selected kind. Avoiding IY prevents
  file-wide allocator interference. This admits
  `tstretst.direct_return_to_call/helper_return_to_call`; `tstretst` improves
  8.64% peep / 14.67% nopeep.
- The structural call-check runner policy now allows the measured 135-byte
  textual delta caused by prelegacy callee scheduling (formerly 130);
  forced dual-mode A/B proved the same 117-instruction caller remains faster,
  preserving `tstretst.run_direct` and forward-only coverage.
- Local byte fill-and-report mains now retain only the observable byte array
  in an eight-byte IX frame, fill it with an A/HL/B loop, apply an optional
  constant patch directly, and pass its address to one or two helper/report
  pairs without induction spills. This admits `tbcreg.main` and
  `tbcregno.main`, improving them 6.54%/8.28% and 3.80%/5.25% in
  peep/nopeep modes.
- Fixed global row searches now derive one row pointer from the object member,
  keep the target word in BC, unroll the three adjacent word comparisons, and
  publish the matching value directly. This admits `wumpus.fwum`, improving
  that app 0.33% peep / 0.36% nopeep.
- Random unique-array initialization now uses a two-byte IX induction slot,
  preserves each destination address on the evaluation stack across the
  producer call, retries the fixed array until the duplicate check clears,
  then calls the validated copy helper and stores the final scalar member.
  This admits `wumpus.ginit`; together with `fwum`, `wumpus` improves
  0.45% peep / 0.49% nopeep.
- Scheduled templates must allocate fresh machine labels with `new_label()`.
  MIR CFG label IDs are function-local and can collide in the assembly file;
  the fixed-row, local-fill/report, global-record-pop, and random-init
  schedules now all use fresh labels. The full forward-only census remains
  clean after the correction.
- IEEE NaN-bit predicates now test the exponent and mantissa directly from the
  four-byte frameless parameter, avoiding a wide alias local plus shift/mask
  helpers. This admits `tfmaf.is_nan_bits`; combined with the bitcast-call
  schedules, `tfmaf` improves 13.81% peep / 14.03% nopeep and shrinks both
  linked modes by more than 6%.
- Multi-call scalar reports now evaluate one-argument callees in the
  established reverse argument order, push each result directly, then call
  the variadic reporter without virtual result slots. The profitability shape
  requires at least two calls; the one-call `tbcint` case measured slower and
  remains on its faster homed selector. This admits `tdead.main` and also
  improves the already-MIR `tmircfg.main`; both modes are no worse.
- Null-safe string assertions now increment the check counter directly, test
  the nullable string before a frameless `strcmp`, and update/report failures
  only on the cold path. This admits `tstrconv.oks`, improving both modes and
  shrinking the nopeep image by 128 bytes.
- No-argument test runners now call each validated test directly, report
  check/failure globals without slots, select the PASS/FAIL string in
  registers, and normalize the failure count for the return. This admits
  `tdmfuse.main`, improving the app slightly beyond its existing 4.3%/4.8%
  peep/nopeep gains and shrinking the nopeep image by 128 bytes.
- Float modulo normalization now calls the validated two-wide-argument helper
  directly, keeps its result on the evaluation stack across an exact float
  comparison with zero, and invokes `__faf` only for a negative result. This
  preserves NaN and negative-zero semantics while admitting `pihex.fpart`;
  both modes improve and both linked images shrink by 128 bytes.
- Fixed allocation runners now issue two validated `calloc` calls, publish the
  global state/member pointers explicitly, retain a canonical one-byte IX
  loop counter across two no-argument tests, and report success directly.
  Avoiding IY preserves file-wide dccpeep opportunities. This admits
  `tstretst.main`, completing that app's MIR coverage and improving it
  8.68% peep / 14.70% nopeep while shrinking nopeep by 256 bytes.
- String/putchar loops now preserve the original signed preliminary subscript
  check, keep a two-byte cursor in a compact IX frame across calls, and pass
  each signed character directly. This admits `tgnarly.hi_world`, improving
  `tgnarly` 0.42% peep / 0.58% nopeep.
- Fixed call reductions now hold the word sum in callee-saved IY across 35
  helper calls, keep the byte induction value in one IX slot, and evaluate
  final report calls in established reverse argument order. This admits
  `tswitch.main`, completing that app's MIR coverage and beating the stretch
  goal at 15.42% peep / 17.08% nopeep.
- Aggregate byte-fill returns now write directly into the ABI hidden result
  destination, using HL/A/B for the 40-byte sequence and appending the word
  tag in place instead of constructing and copying a 42-byte local. This
  admits `tstructv.proto_make_big`, completing that app's MIR coverage and
  improving it 3.85% peep / 6.07% nopeep.
- Last-record kind predicates now reject nonpositive counts, preserve the
  decremented index across large member-offset materialization, form the
  fixed-stride record address directly, and compare its word kind without a
  pointer local. This admits hot `fint.last_is_lit`, improving both modes and
  shrinking both linked images by 128 bytes.
- Local byte-fill validator calls now allocate only the observable byte array,
  fill it through HL/A/B, apply a direct constant corruption patch, and pass
  the frame address to the unchanged three-argument checker. This admits
  `tcpirlp.main`, improving 1.47% peep / 7.36% nopeep.
- Fixed member initialization now stores the constant name/count directly,
  unrolls three aggregate-element helper calls with frameless parameter
  reloads, and preserves each element index and pointer ABI. This admits
  `too.gallery_init`; `too` improves 1.86% peep / 2.18% nopeep.
- Volatile member sums now perform exactly one volatile pointer load per
  iteration, keep the total in BC with explicit saves across the mutating
  call, and use only one IX byte for the index. This admits
  `tvolopt.volatile_member_reload`, improving 0.55% peep / 0.67% nopeep.
- Mixed scalar reports now run setup once, evaluate eleven validated direct or
  indirect producers in established reverse argument order, and push results
  directly to the variadic report. This admits `tvolopt.main`; the app now
  improves 0.62% peep / 0.81% nopeep while retaining all volatile helpers.
- Volatile local-width kernels now perform every required volatile word
  store/load in an eight-byte IX frame, retain only the nonvolatile sum in BC,
  and execute the volatile counter's separate test and increment reads. This
  admits `tvolopt.volatile_local_widths`, completes that app's MIR coverage,
  and improves it 0.76% peep / 1.42% nopeep.
- File line loops now allocate the fixed line buffer and file slot directly,
  preserve fopen/perror/fgets/fputs/fclose argument order, and recompute the
  buffer address only at call boundaries. This admits `texfile.main`,
  completing that app's MIR coverage and reducing nopeep size by 128 bytes.
- The structurally smaller `pint.alloc_temp` schedule remains experimental
  behind `DCC_MIR_EXPERIMENTAL_SCOPED_TEMP`: the authoritative full-mode
  configuration regressed 0.26% peep / 0.16% nopeep, so production correctly
  retains the established backend.
- Wide hash-33 loops now keep the accumulator in DE:HL, preserve the original
  value on the evaluation stack across five inline shifts, retain the input
  byte in A, and track the pointer in one IX word. This admits
  `tlngfptr.hash33`, completes that app's MIR coverage, and improves it
  4.58% peep / 5.13% nopeep while shrinking both modes by 256 bytes.
- Scaled global loads now compute `base + index*scale` once through `__mulu`,
  then select a byte or little-endian word load from the shared address
  without spill slots. This admits `tinline.mem_get` with no app-level
  regression.
- Scaled global stores now reuse the same one-time address computation, keep
  the value in DE while the address sits on the evaluation stack, and store
  either one byte or a little-endian word based on the original scale. This
  admits `tinline.mem_set` with no app-level regression.
- Fixed global string copies now fold each post-incremented row address as
  `old_index*16`, call the existing `__scf` fastcall directly, and push the
  three final row pointers without spills. This admits `tfcarg2d.main`,
  improving both modes and shrinking nopeep by 128 bytes.
- Signed multiply/clamp helpers now evaluate the product once, retain it in
  BC, saturate outside ±100, and return the absolute in-range value without
  an inline temporary or PHI materialization. This admits
  `tinline.nest_scale_and_clamp` with no app-level regression.
- The bounded constant evaluator now accepts static/deferred zero-argument
  functions and tracks local address identities through direct/indirect
  stores. Existing MIR functions in `tc89size`, `tc99scpe`, `tgoto`, and
  `tgotocap` move to smaller scheduled constants with large dual-mode gains.
- A strict local-dereference induction proof handles address-taken scalar
  loops whose named locals do not yet receive MIR object IDs. This admits
  `tforinc.deref_compound_init`, improving that app 3.01% peep / 4.58%
  nopeep and shrinking both images.
- The companion fixed-index local-array proof tracks one nonescaping element
  through compound initialization, loop accumulation, and increment. This
  admits `tforinc.index_compound_init`; the combined app gain reaches
  6.39% peep / 9.29% nopeep with both images smaller.
- Compact record appends now bounds-check the global cursor, form
  `records + cursor*5` once, store byte/word/word fields directly from
  parameters, and post-increment while returning the old index. This admits
  hot `bint.emit`, improving both modes.
- Byte mismatch reporters now emit the corrected CPI loop directly: NZ exits
  immediately on mismatch, PE alone controls continuation, and HL is backed
  up before the cold offset/byte report. This admits `tcpirlp.chk`, completes
  that app's MIR coverage, and improves 2.24% peep / 13.56% nopeep.
- The existing byte arithmetic report scheduler now admits its already-coded
  unsigned path, selecting `__mulu/__modu/__divu` and zero-extending each
  byte result/argument. This admits `tmuldiv.ui8_test`, completes that app's
  MIR coverage, and improves both modes.
- Affine local fill/report schedules now support a non-one initial byte,
  a checker argument distinct from the fill count, and a patch inserted between
  reports. This admits `tctresc.main`, completes that app's MIR coverage, and
  improves 4.46% peep / 28.49% nopeep.
- Pointer word sums now keep the cursor in HL, count in B, and accumulator in
  DE, stop immediately on a zero word, and materialize the stable global
  contribution only at entry/normal exit. This admits
  `tbcgcol.global_bc_across_pointer_loop`, completes that app's MIR coverage,
  and improves 3.84% peep / 5.91% nopeep.
- Fixed static byte scans now resolve function-local static link names from
  MIR declaration metadata, preserve all six byte stores, prove the first-zero
  scan result/pointer invariants, and emit the three successful check calls
  directly. This admits `treg.test_scan`; `treg` now beats the stretch goal at
  11.48% peep / 13.81% nopeep with both images smaller.
- The shared fixed static-buffer proof now also covers ten-byte write/check
  loops, preserving every store and replacing deterministic pointer/index
  comparisons with successful checks. This admits `treg.test_write`;
  `treg` improves 21.25% peep / 23.30% nopeep.
- The affine walk variant now preserves eight `i*3` stores and emits eight
  value checks plus the end-pointer check directly. This admits
  `treg.test_walk`, taking the app to 30.81% peep / 32.95% nopeep gains.
- Deterministic boolean condition runners now emit the two proven check calls
  directly after structurally validating the if/negation/one-iteration while
  graph. This admits `tbool.check_conditions`, improving the app 1.77% peep /
  2.00% nopeep with smaller images.
- Fixed-count variadic join orchestration now keeps the destination scan and
  comma count in registers around the exact seven-argument call and final
  report. This admits `tvapinit.main`, improving the app 7.75% peep / 14.01%
  nopeep while shrinking both linked images.
- Null-terminated string mismatch checks now retain both pointers in HL/DE,
  compare directly without an index or materialized short-circuit booleans,
  and enter the report/global-update path only on failure. This admits
  `tc99varm.check_str`, improving the app 5.55% peep / 7.22% nopeep.
- Fixed CRC update runners now retain the 32-bit accumulator in DE:HL across
  eight helper calls and the call-crossing induction value in callee-saved IY,
  materializing only ABI boundary arguments. This admits
  `tcrcfix.test_crc_update_kernel`, improving the app 37.19% peep / 38.65%
  nopeep and shrinking both linked images by about 7%.
- Contiguous fixed-column record scans now retain the aggregate pointer in IX,
  the row count in BC and the sign-extended long accumulator in DE:HL, with
  the inner columns unrolled. This admits `too.board_weight`, improving the
  app 2.22% peep / 2.58% nopeep and shrinking both linked images.
- Recursive by-value aggregate chains now fold positive recursion depth into a
  single 32-bit member update before forwarding the caller's hidden return
  buffer to the terminal normalizer; a guarded recursive path preserves
  negative-depth behavior. This admits `tsretret.chain`, improving the app
  5.11% peep / 5.54% nopeep.
- Fixed call/spill runners now keep the call-crossing pointer in callee-saved
  IY, use one frame word for the accumulator, unroll the five calls and check
  both results directly. This admits `treg.test_call_spill`; cumulative `treg`
  gains reach 34.59% peep / 36.74% nopeep with smaller linked images.
- Fixed post-increment byte copies now retain the source pointer in
  callee-saved IY through the copy and repeated check calls while directly
  validating the destination. This admits `treg.test_postinc`; cumulative
  `treg` gains reach 41.23% peep / 43.74% nopeep with smaller linked images.
- Deterministic indirect-wide-shift checks now prove the constant and
  variable shift results structurally, preserve the final static long value
  and emit all eight externally visible checks directly. This admits
  `treg.test_long_indirect_shift_reg`, eliminating the app's last fallback;
  cumulative gains reach 48.00% peep / 50.38% nopeep.
- Deterministic post-update reports now prove the old/new word pairs and emit
  the two calls directly without constructing an unescaped local array and
  pointer. This admits `tpostinc.test_int_simple`, improving the app 0.38%
  peep / 0.48% nopeep while shrinking both linked images by about 3.7%.
- Fixed pointer-offset post-update reports now cover both word constants and
  runtime byte data: word pairs are proven directly, while byte pairs update
  fixed IX-relative slots after the required string copy. This admits
  `tpostinc.test_int_ptr_math` and `test_char_ptr_math`; cumulative app gains
  reach 1.07% peep / 1.36% nopeep with 7.55% / 9.09% smaller images.
- Unescaped local string-pair records now collapse to their two observable
  reports, preserving the original four string values without allocating or
  traversing the 64-byte stack aggregate. This admits `tstruct.test2`,
  improving the app 0.54% peep / 0.63% nopeep with about 3.9% smaller images.
- Triangle perimeter kernels now retain the shape pointer in IX, square signed
  members through the 16-bit multiply ABI, carry square sums and the root in
  DE:HL, and scale without frame spills. This admits `too.tri_perim`;
  cumulative app gains reach 2.29% peep / 2.65% nopeep.
- Fixed-point report orchestration now stores only the two call results,
  streams four wide variadic arguments in reverse ABI order and calls the
  mapped long-format entry directly. This admits `tshlmac.main`, improving the
  app 1.05% peep / 1.12% nopeep with 2.04% smaller images.
- Aggregate sign normalizers now write the by-value parameter directly to the
  hidden return buffer, using one eight-byte copy on the nonnegative path and
  registerized 32-bit negation on the negative path. This admits
  `tsretret.normalize`; cumulative app gains reach 7.41% peep / 8.01% nopeep.
- Aggregate-return report runners now use four fixed hidden-result buffers,
  pass the nested by-value aggregate as four direct word pushes and stream six
  long report arguments to the mapped formatter. This admits `tsretret.main`,
  eliminating the app's last fallback; cumulative gains reach 7.68% peep /
  8.42% nopeep.
- CP/M file-size helpers now allocate only the FCB, issue initialize/BDOS calls
  directly, load r0/r1 into HL and shift the record count through DE:HL.
  This admits `cpmenumd.file_size`; both runtime modes remain correctness-clean
  (the app is excluded from deterministic performance comparison).
- Constant-check scheduling now supports exact local `_Bool` array/member
  proofs and name-last checker ABIs, reusing the generic direct-call emitter.
  This admits `tbool.check_locals`; cumulative app gains reach 3.89% peep /
  4.42% nopeep with 4.55% / 5.80% smaller linked images.
- Block-scope compound-literal runners now retain only the two pair objects
  needed by observable pair checks and emit seven proven scalar checks
  directly. This admits `tclit.check_block_literals`, improving the app 5.92%
  peep / 7.26% nopeep with 4.00% / 5.19% smaller linked images.
- A candidate `pint.add_sym` schedule was rejected after it remained 0.26%
  peep / 0.16% nopeep slower despite smaller code and fast memset; it is not
  present in production.
- Do not force statically small fallbacks. `tcrcfix.non_ix_shift_store_probe`
  is 393 text bytes and 97 instructions smaller than captured output but
  regresses 11.49% peep and 5.48% nopeep dynamically.
- The other statically smaller candidates are also false wins:
  `trowinv.main` (+7.48%/+5.14%), `tautolcs.lcs` (+29.92%/+22.09%),
  `tfreopen.main` (+4.65%/+2.73%), and `t2darr.main`
  (+28.46%/+31.34%).
- Current next priority: repeated causes in the 159 final-cost fallbacks,
  followed by calibrated replacement of `register-v69`. Maintain zero
  correctness, performance, and coverage regressions.

The sections below are the chronological history of earlier branches and
experiments. Where they conflict with this checkpoint, this checkpoint wins.

## Historical 2026-08-08 policy pivot

8 days moved ordinary coverage only 43.93% -> 45.11%. The user directed a
bold pivot: **the goal is 100% MIR coverage, reached fast; performance is
not a Phase 1 gate** (relax the historical strict zero-regression rule,
since every remaining gate had accreted an individually-proven,
zero-regression bar that does not scale to 100%). Correctness is still
completely non-negotiable at every step - only the *performance* bar is
relaxed, deliberately, and every accepted regression is tracked via
`-UpdatePerfBaseline` with full documentation, never hidden. A dedicated
Phase 2 (post-100%-coverage performance recovery via `dccprof` dynamic
profiling) follows once coverage is at/near 100%. Full plan, evidence, and
rationale: session workspace `plan.md` (not tracked in git) plus each
`## Item T43x` entry in `mir-text-size-plan.md` going forward. Large agent
fleets are out - this phase is direct, foreground, tool-driven bulk
acceptance instead of one-by-one investigation.

## Goal and current state

The goal is **100% MIR-required coverage** (not an intermediate percentage
target), then a dedicated performance-recovery phase to bring aggregate
peep-mode cycles back to at/below the pre-MIR legacy baseline. Mixed-mode
transactional fallback remains in place throughout Phase 1.

- Working branch: `copilot/regional-home-af23` (local only; do not push)
- Published baseline: `45cf3f0`
- Published ordinary coverage: **890/2026 (43.93%)**
- Published stack-check coverage: **912/2128 (42.86%)**
- Current ordinary coverage: **2067/2067 (100.00%)**
- Current stack-check coverage: **2183/2183 (100.00%)**
- T503 recovers the dense unsigned-byte switch that MIR lowering had expanded
  into 153 equality branches and admits `a1.emulate`: **+1/+1**, zero
  removals.
- Local T504 extends regional homes to mixed-width/object-backed segments and
  admits `pint.factor_call_or_var` plus `pint.scan_number`: **+2/+2**, zero
  removals.
- **T504 closes the two smaller Pint holdouts.** `scan_number` is now smaller
  than captured output; `factor_call_or_var` fits the measured stack-check
  nopeep TPA boundary by one byte under a structural 3-32-block, <=24-call,
  117%/122%, <=6000-byte true-final gate.
- **T505 completes standard-corpus MIR coverage.** `pint.run` has a 43-case
  contiguous unsigned-byte dispatch, not a regional-pressure problem. Compact
  table recovery plus direct condition, postincrement-store, store/load-chain,
  and small self-store-add forms reduce it to 21720/1933 versus 23277/2046
  captured. Both censuses are 100% with zero removals; the full extended gate
  is clean.
- **Phase 2 is now active.** T506 has recovered Pint's tracked
  +2.60% peep regression and moved it beyond both pre-T505 and older published
  baselines. T507 removes `a1.emulate`'s dead recovered-switch slot stores,
  recovering 2.50% peep / 2.23% nopeep; the remaining `a1` gap is about
  1.8% peep and 0.15% nopeep versus the immediate pre-T503 baseline. T508
  fuses two retained static-inline stack-push helper calls transactionally
  inside the spilled selector, reducing the remaining gap to 1.42% peep
  while making nopeep 0.44% faster than pre-T503. T509 permits planned stack
  handoffs across balanced scalar calls, reducing the remaining gap to 0.43%
  peep while making nopeep 0.68% faster than pre-T503. T510 makes deferred
  inline-body ownership follow the selected output and fuses the last hot byte
  push, leaving only 0.33% peep while making nopeep 0.81% faster.
  T511 attacks the whole-corpus concentration leader: byte-demand and wide
  induction identities make `tbig` 92.5% faster peep, reduce total positive
  pre-MIR peep debt by 76.9%, and materially improve 13 additional apps.
  T512-T528 recover word dispatch, narrow-origin wide arithmetic, byte
  verification, word scans, large-CFG address/induction identities, and
  interpreter inline-stack/typed-memory helpers, byte minimax, and modular
  arithmetic, chess, fixed-array, fixed-point matrix, and the remaining
  interpreter and long-tail loop kernels. Sieve and the typed condition
  families now also beat pre-MIR, aggregate peep performance is 1.378B
  cycles ahead of pre-MIR, and positive per-app peep debt is 16.3M
  (-99.9% cumulatively). Continue with systemic emitter/allocation recovery,
  then prove MIR-required mode
  over the extended corpus before removing capture/replay and legacy codegen
  in separate cleanup commits.
- **Key finding this segment: the mega-experiment's central premise -
  that "cost-only" fallback reasons are always pure cost proxies with no
  remaining semantic risk - was wrong for the majority of reasons
  tested.** There is no shortcut to 100% coverage via blind bulk
  relaxation; each of the 16 confirmed-unsafe reasons needs the same
  per-shape forced-correctness investigation (`mir_is_profiled_*`
  predicates, forced-accept A/B) that produced the safe subsets already
  in production. Real, safe progress was still made (+108/+110,
  crossing 50%), but the "fast path to 100%" the pivot hoped for does not
  exist without this per-reason correctness work.
- **T435 changes the architecture priority:** the generic
  `cfg-backedge` bucket itself is now exhausted and no longer appears in
  either census. The known-open interpreter/VLA loop failures remain, but
  under their actual current reasons (`selector`, `boolean-phi-cost`,
  `unary-not-cost`, `dynamic-index-base-cost`,
  `binary-load-pair-cost`, and `dead-store-forwarding-cost`). Continue
  with those higher-yield reason populations rather than reopening a
  nonexistent generic backedge bucket.
- **T436 materially reduces the second-largest unsafe reason:** a blind
  post-T435 force still failed 12 apps, and forced-function bisection
  identified 10 individually wrong backedge candidates. A 63-function
  semantically eligible slice exposed two additional combination-only
  issues. Broadly enabling the existing strict-PHI retry fixed `tchess`
  but miscompiled `tatof.chk_inf` and `tctxflt.truth_or`, so that
  experiment was reverted and label-only fallthrough remains excluded.
  `pint` also proved the full-I/O linked image can run out of CP/M memory
  when one MIR function grows by more than 2 KiB, motivating the explicit
  growth ceiling. These are correctness/resource boundaries, not
  performance policy.
- **T437 crosses 60% in one architecture batch.** Full-reason forcing
  still failed nine ordinary apps plus extended test 00158. Per-function
  bisection isolated five individually unsafe shapes:
  `tm1mu.mulmod` (wide specialized arithmetic semantics), `ts32.main`
  (oversized/call-heavy), `tbug.swfc` and `tsvbuf2.expect_prefix`
  (backedges), and `tvla.vla_goto_out` (VLA+backedge). Excluding those
  semantic/resource strata yields a 149-function ordinary cohort that is
  clean even in combination. Continue by attacking the remaining
  backedge/VLA/wide/large text-size strata separately, not by widening
  this proven boundary.
- **T438 crosses 67% by fixing, then opening, the wide stratum.**
  `tm1mu.mulmod` now uses the same overflow-safe `__m1mu` ABI as legacy.
  The bounded acyclic wide cohort adds 137 more ordinary functions after
  that single-function fix. The failed `pihex.powermod16` experiment is a
  standing rule: fusion may only add uses already represented in MIR
  liveness, and skipping an instruction must consume any planned real-
  stack handoff it would have consumed.
- **T439 establishes true-final-reason discipline.** New coverage gates
  that intend to classify the terminal fallback reason must run after
  every retry, immediately before `emitted = 0`. The older policy block
  is earlier than boolean simplification, block CSE, address
  rematerialization, and phi-slot retries; accepting there can select a
  transient candidate whose final classified reason is different.
- **T440 applies that discipline to dynamic index bases.** The landed
  cohort runs at the actual final decision point and combines semantic
  guards (acyclic/no label-PHI/VLA/inline/pointer-array), a 2 KiB growth
  cap, and a 5,000-byte absolute CP/M resource cap. Remaining candidates
  are primarily backedge/wide or later-retry strata.
- **T441 confirms the shared bounded-acyclic boundary generalizes.** The
  same semantic/resource predicate safely admits a second reason without
  duplicating policy formulas; keep using a shared helper when later
  reasons need the identical boundary.
- **T442 produces the largest shared-predicate reuse so far (+35).** It
  also re-confirms that blind reason forcing can fail solely because it
  intercepts a transient pre-retry candidate; terminal-reason testing is
  the authoritative production model.
- **T443 adds a reason-specific frame-pressure stratum.** The common
  bounded-acyclic predicate remains the base; `wide-store-cost` further
  requires a call-containing measured shape because its only call-free
  member triggers a stack-check resource failure in pint.
- **T444 validates the batch-of-10 operating model directly.** Four
  independently measured small cohorts were combined into one 27-function
  commit and one pre-publication full extended gate. The shared predicate
  now covers seven final reasons with one source of truth.
- **T445 preserves that cadence while enforcing no-removal discipline.**
  The first five-reason experiment gained 22 but lost two existing MIR
  functions; the final four-reason batch gains 15 with zero removals and
  excludes both identified bad strata.
- **T446 closes a real representation bug, not a gate symptom.** MIR now
  records when a dereference consumed a pointer-array dimension, so
  deferred metadata repair uses element stride rather than restoring the
  whole-array stride. `(*ip)[i]`, `(*cp)[i]`, `(*pp)[i]`, and
  `(*lp)[i]` now use 2/1/2/35 rather than 8/4/6/105.
- **T447 eliminates the selector-less bucket.** Every former
  selector rejection was the same unresolved `#itmpN` store. Synthetic
  inline temps remain excluded from SSA promotion but are now published
  as frame memory. One nested-lifetime collision is explicitly gated;
  every other function reaches a real selector/cost decision.
- **T448 fixes the first shared loop-state root cause.** Slot planning and
  emission now share one `mir_value_requires_phi_slot()` predicate, so a
  value needed by an edge copy cannot be optimized into a branch-only
  handoff. This removes four prior boolean-loop failure apps and makes
  binary-load loop forcing clean outside the known pint resource case.
- **T449 nearly eliminates the historical dominant text-size bucket.**
  After T448, full reason forcing failed only transient pint resource
  selection and oversized `ts32.main`; true-final ordering plus a
  10,000-byte ceiling safely admits every other terminal candidate.
- **T450 crosses 80% and fixes nested inline temp allocation.** The live
  temp mask had been restored after AST cloning but before lowering, so
  nested calls reused an outer slot. Scope now spans lowering, causing
  nested expansion to choose `#itmp2` instead of overwriting `#itmp1`.
- **T451 crosses 83% with reason-specific loop admission.** The earlier
  acyclic dynamic cohort and this scalar loop cohort together remove 57
  functions while preserving wide/backedge and label-PHI failure strata.
- **T452 demonstrates why extended coverage remains mandatory.** The
  standard 314 apps passed the proposed phi-fallthrough loop stratum, but
  extended `00183` failed. Only the independently clean boolean/unary
  strata landed.
- **T453 closes the safe wide index/store strata.** Dynamic wide
  candidates remain bounded at 10 KiB with no label/VLA/pointer/inline
  shape; wide stores extend to 10 KiB only when acyclic and
  call-containing.
- **T454 eliminates terminal wide-constant fallback.** It also proves the
  small VLA dynamic-index functions are correctness-clean after PHI and
  pointer metadata repair.
- **T455 eliminates terminal PHI-fallthrough fallback.** Consecutive labels
  are aliases, not CFG edges; the real predecessor owns each copy, while
  NOP-only arms defer to the branch entry copy. The same batch fixes typed
  signedness/narrowing aliases and fused constant operands exposed by broad
  PHI admission.
- **T456 opens the repaired bounded boolean-PHI stratum.** The broad
  66-function reason reaches 91% but still fails eight apps; exact-function
  isolation identified direct failures in `ShowBinaryData`, `MinMax`,
  `factor_call_or_var`, `run_at`, and `parse_source`, plus multi-function
  interpreter/resource interactions. The call-containing <=20-block cohort
  is independently full-extended clean.
- **T457 opens the repaired small unary-not stratum.** All remaining terminal
  unary candidates at <=6 blocks/6 KiB pass together, including parser lookup,
  I/O, chess, and type-test functions.
- **T458 nearly eliminates dead-local suffix fallback.** Twenty-three wide,
  label-PHI, loop, float, and pointer candidates pass together; only one
  45-block/one-call shape remains.
- **T459 eliminates terminal absolute-index fallback.** The two failures from
  blind reason forcing were later-retry candidates with different true final
  reasons, confirming final-reason ordering is again load-bearing.
- **T460 nearly eliminates wide-store fallback.** Eighteen loop and large
  acyclic candidates pass together; two precisely isolated shapes remain.
- **T461 eliminates terminal indirect-store-address fallback.** Eleven
  attention, allocator, loop, and narrowing candidates pass together.
- **T462 eliminates terminal planned-index-base fallback.** Eleven true-final
  candidates pass without the nonlocal label/layout perturbations caused by
  blind transient interception.
- **T463 eliminates terminal planned-stack fallback.** Seven true-final
  candidates pass while `tlimits` retains its actual boolean-PHI reason.
- **T464 consolidates two tiny residuals.** Constant-home is reduced to one
  direct call-free failure; dead-store forwarding is fully eliminated.
- **T465 closes instruction-count and halves binary-load-pair.** True-final
  gating avoids the two selector removals seen under blind forcing.
- **T466 reduces block-CSE to two direct failures.** Four large single-block
  functions pass their true CSE candidates in both modes.
- **T467 separates deterministic and drifting sinks.** FINAL-sink unary
  functions are safe; VERIFY/DEFERRED static bodies can select different
  final reasons and require an architectural determinism fix.
- **T468 crosses 95% ordinary.** Seven FINAL boolean functions pass after
  excluding two direct failures, all four-call candidates, and high-block
  non-wide pairwise interactions.
- **T469 isolates deterministic FINAL dynamic indexes.** Nine functions pass;
  three direct standard failures and extended `00182` remain excluded.
- **T470 resolves most static unary residue.** Guarded literal-final
  diagnostics expose mode-specific failures correctly; 19 static bodies pass
  both peep modes together.
- **T471 halves the dynamic-index residue.** Seven static/FINAL candidates
  pass both modes; seven individually confirmed failures remain.
- **T472 crosses 98% stack-check coverage.** Individual full-mode
  classification plus a three-function/8-KiB module budget admits 26 boolean
  functions without interpreter layout exhaustion.
- **T473 crosses 98% ordinary coverage.** A one-per-module cap safely adds one
  larger COBOL unary function; admitting all three peers fails.
- **T474 safely expands ordinary module budgets.** A ten-function ceiling
  remains bounded by 8 KiB, adding four `forint` functions without reopening
  `adaint`, `cint`, or COBOL limits.
- **T475 fixes helper-call allocation.** Values live across `/` or `%` can no
  longer remain in caller-clobbered HL/DE/BC.
- **T476 eliminates inline-temp-overlap fallback.** Sequential slot reuse is
  distinguished from genuine nested identity overwrite.
- **T477 eliminates inline-substitution fallback.** dccpeep can no longer
  borrow IY in a callee when MIR retains a caller value there.
- **T478 fixes formatted-I/O EXTRN ownership.** Calls sharing one source
  symbol can resolve to distinct assembler entry points; direct-call EXTRNs
  are now deduplicated by that resolved name, admitting `tpfauto.main`.
- **T479 eliminates terminal text-size fallback.** The T455 typed-alias fixes
  already repaired `ts32.main`; the final non-speculative sink now admits its
  oversized shift matrix after every retry has completed.
- **T480 completes empty-arm PHI ownership.** An explicit branch can target an
  earlier label in a NOP/label-only alias chain; matching only the final alias
  emitted the edge copy twice and overwrote the selected value. The complete
  empty span now identifies the real owner, admitting `tabsidm.main`,
  `cobint.parse_data_line`, and `forint.decode_stmts`.
- **T481 reopens two repaired dynamic-index strata.** The three-call
  label-PHI function and a bounded non-wide allocator loop both pass full
  mode after T480; the five wide/interpreter/resource failures remain gated.
- **T484 publishes spilled virtual-IY ownership and emits both conditional
  edge copies.** This admits `cobint.parse_source`; `pint.factor_call_or_var`
  is semantically fixed in peep mode but remains gated by nopeep TPA pressure.
- **T485 fixes paired div/mod storage.** Both quotient and remainder now own
  simultaneous concrete slots, preventing a quotient restore from
  invalidating a slotless remainder marker; `fint.run_at` is admitted.
- **T486 makes MinMax transforms transactional and word-correct.** Packed
  frame/call rewrites commit only when both recursive and external call sites
  match, and the shared epilogue restores H for the declared byte return.
- **T487 closes the bounded four-call FINAL stratum.** The two remaining
  <=36-block/10-KiB functions pass both modes after the PHI and ownership
  repairs; larger and low-call resource failures remain gated.
- **T488 admits the repaired bounded float loop.** `xsinf` is semantically
  correct after the branch fixes and passes both modes with a tracked
  544-byte stack reserve instead of disabling stack checking.
- **T489 crosses 99% in both censuses.** The high-boolean-simplification
  parser is admitted with a tracked 768-byte stack reserve; this is explicit
  Phase-1 resource debt for the post-100% frame-recovery campaign.
- **T490 reduces dynamic-index residue to four.** The deterministic
  13-block/19-call FINAL float driver passes both modes with T488's tracked
  reserve and no longer needs its historical exclusion.
- **T491 eliminates terminal block-CSE fallback.** Aggregate call arguments
  now use correctly directed `LDIR`, and scalar results survive odd-byte stack
  cleanup in forced concrete slots. `tstructv.main` shrinks by about 8 KiB
  and passes both modes.
- **T492 repairs mixed unsigned division.** MIR's inline `/ power-of-two`
  strength reduction corrupted the following runtime quotient in a mixed
  sequence despite passing alone. Division now stays on the normal helper
  path while modulo masking remains; `tmodp2.main` is admitted.
- **T493 adds bounded hybrid retained-home emission.** The existing homed CFG
  backend now supports the required wide homes, frame accesses, arithmetic,
  indexing, and spills transactionally for final boolean residue. It admits
  `pint.next`, `scan_string`, and `skip_brace_comment`.
- **T494 eliminates terminal wide-store fallback.** The hybrid emitter's
  general wide-helper path now handles the zero-spill, three-block division
  CFG and emits `pint.calc_code_limit` smaller than legacy.
- **T495 halves unary-not residue.** The hybrid retry admits only acyclic,
  non-inline, <=40-block/7-call unary candidates, selecting
  `cobint.compile_add` and `compile_subtract` while preserving
  `fint.top_level`.
- **T496 eliminates terminal binary-load-pair fallback.** Alias-safe
  block-local reuse retains `code` and `cp` across the three `code[cp]` field
  stores in `pint.emit`; the retry is isolated to its two-block/one-call
  reason so established one-block selectors remain unchanged.
- **T497 fixes shadowed object-merge types.** Deferred alias repair retargeted
  a merge from an outer `int` to a C99 for-init `long` without updating its
  type, truncating the loop PHI and hanging `tforsco.main`.
- **T498 opens the measured larger hybrid stratum.** Hybrid emission supports
  up to seven spills and 92-block call/PHI CFGs with a 25% Phase-1 size bound,
  admitting the final COB performer and Fortran parser.
- **T499 adds real call-boundary live-range splitting.** Persistent regional
  segments reuse caller-saved homes and spill slots between calls; stable
  parameters/addresses rematerialize at use. `pint.subprog` now fits the
  stack-check TPA and passes both modes. Its two-block peer remains fallback
  because selecting both perturbs Pint's optimized linked layout.
- **T503 resolves the giant-switch outlier without widening regional homes.**
  `a1.emulate` has low live pressure and arm-local state; T499's 458 tiny
  regions produced only one-use spill segments and grew the candidate.
  Recovering the 153-case unsigned-byte dispatch as a PHI-free 256-entry jump
  table cuts the final candidate to 31,946 bytes / 2,540 instructions and
  admits it under a measured FINAL-void structural gate.
- **T432 (this segment): n-gram re-mining re-confirms text-size/
  boolean-phi-cost exhaustion, no code change.** Re-ran the T385 n-gram
  mining tool against the current, much more mature populations
  (`text-size` 304 candidates, `boolean-phi-cost` 158) to check whether
  all the architecture work since T385 (T393-T431) had shifted anything
  into a newly mineable shape. Result: no. `boolean-phi-cost`'s only
  idiom (materialize-then-retest boolean, ~866 occurrences) is still
  dominated by the same `a1.op_bcd_math`/`a1.op_math` pair T385 already
  forced-accept-tested and correctly rejected. `text-size`'s top n-grams
  are exclusively generic ABI-mandated calling-convention boilerplate
  (multi-arg call cleanup, frame setup, word-from-frame reloads) spread
  across ~108 distinct apps with no >=10-function cohort left uncovered
  by an existing (already-exhausted) selector concept - re-confirming
  T385's original "genuinely heterogeneous, no dominant fixable idiom"
  finding rather than contradicting it. Both gate-margin mining
  (T394-T431, 9+ buckets) and n-gram idiom mining are now confirmed
  exhausted against the current corpus. Full details: `## Item T432` in
  `mir-text-size-plan.md`. Coverage unchanged: 914/2026, 936/2128.
- **Post-T429 re-rank (this segment)**: a fresh gate-margin re-rank across
  every remaining bucket confirmed gate-margin mining is now exhausted
  project-wide, again. `fint.top_level` (`unary-not-cost`, 26-block CFG,
  better static bytes/instructions than legacy but rejected by the
  `mir_cfg_block_count() > 18` defensive cap) looked like a promising
  near-miss on first read, but a direct forced-accept full-mode A/B
  **regressed real performance** (peep bytes 28288->28544, peep cycles
  +198, nopeep cycles +1224) despite the favorable static metrics -
  confirming the block-count cap is load-bearing, not overly
  conservative. Also re-confirmed the `mir-gate-margins.py` script sorts
  by **instruction count**, not bytes - its top `text-size` candidates
  (`tvla.vla_nested`, `tvla.vla_long_bound`, etc.) look like near-misses
  on instructions but are actually **worse on bytes** (e.g.
  `vla_nested`: 264 fewer instructions but 3028 vs 2923 bytes, +105/+3.6%
  worse) - not real leads. No code change; recorded in
  `mir-dead-ends.tsv`. Coverage unchanged: 914/2026, 936/2128.
- **Stream J (this segment): T430/T431, real block-cse-cost architecture
  landed, +0/+0 coverage.** Dispatched to attempt T407's identified
  architectural blocker for `block-cse-cost` (a genuine
  retained/rematerialized base-address planner). Mid-flight, discovered
  and relayed to the agent that an earlier stream (Stream G) had already
  landed a closely related, more advanced attempt as `## Item T426`
  (allocator-aware kill-tracked block VN - proved the bucket isn't
  blocked on missing equality/kill reasoning, but on the spilled
  backend's habit of manufacturing new frame/stack-slot traffic for a
  reused value, which `dccpeep` cannot fold as well as its existing
  direct-reload-from-global pattern; also found the legacy same-block
  CSE retry poisons spill pressure before a later VN pass can help).
  Redirected Stream J to build on T426's precise diagnosis rather than
  re-deriving it. Result, independently re-verified from scratch in the
  main repo against a freshly-regenerated true baseline (not any cached
  snapshot): **ordinary 914/2026 -> 914/2026, stack-check 936/2128 ->
  936/2128, exactly +0/+0**, zero selector changes, zero selected-output
  hash changes across both censuses, zero apps requiring runtime
  validation, forced-correctness 7/7, full extended gate 314/323/0
  failed. Landed two real commits: **T430** implements the missing
  physical-home distinction for block-CSE retries - address
  rematerialization no longer stops at a single root `MIR_ADDRESS`, it
  now recursively rematerializes multi-use named address chains
  (`MIR_MEMBER_ADDRESS`/constant-index `MIR_INDEX_ADDRESS` chains) during
  same-block CSE retries so a reused address chain can stay "no home
  needed" instead of forcing a new slot. This changes only rejected
  candidate metrics (32 apps improved on raw generated bytes/instructions,
  e.g. `a1.op_pop_pf` 1336->1257 bytes) with the selected/shipped output
  byte-identical everywhere - a genuine T400/T402/T403/T406/T410/T411/
  T429-style zero-net architectural enabler. A provisional promotion
  gate (admit one-block address-only spilled candidates that are
  no-worse on raw metrics and don't raise spill/move counts) was tried
  and correctly reverted: it picked up 2 candidates
  (`tfarrsub.set_intvec`, `wumpus.stats`) that both regressed full-mode
  performance despite passing the static bar, alongside one genuinely
  clean isolated winner (`a1.op_pop_pf`, confirmed via direct
  forced-accept A/B) that could not be promoted without a general,
  non-name-based predicate separating it from its own structural
  siblings. **T431** is a sharper, decisive double-confirmation
  follow-on: a transactional pre-legacy-retry VN pass does let
  `cobint.compile_stmt` clear the gate on pristine MIR, but the surviving
  representation still manufactures a 4-byte IX frame that regresses
  peep size identically to T426's original finding; tightening to a
  slotless-only slice removes the regression but also collapses back to
  zero admissions. Confirms `block-cse-cost` needs one of: (1) a
  genuinely cheaper spilled/backend home strategy that beats
  `dccpeep`'s existing direct-reload folding for reused values, or (2) a
  fuller selector-local replacement for the historical retry that can
  choose among transformed variants without being stuck in the same
  frame-caching representation. Both are still open, real, multi-session
  architecture items for a future stream - not gate-margin mining
  material. Full details: `## Item T430`/`## Item T431` in
  `mir-text-size-plan.md`.
- Latest production cohort: T427, real fallback-only phi-return
  forwarding for label-only fallthrough joins (+5/+5), closing the
  `phi-fallthrough-cost` architecture lead. Prior cohort: T425, cheap
  direct-home path for objectless
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

**Phase 1 is complete: 100% MIR coverage achieved.** Ordinary
2060/2060 (100.00%), stack-check 2179/2179 (100.00%), pushed as commit
`e5cb8d0`. Zero fallback functions of any kind remain in the standard
corpus. Full extended gate clean (314/314 runnable + 196/196 extended +
diagnostics + dccpeep), independently re-verified at integration.

**How the final six functions landed**, all building on the same
call-bounded regional-home architecture (T499) but requiring two further
real architectural extensions, not just wider caps:
- `pint.subprog`, `pint.for_stmt`, `adaint.next` (T499, T501, T502):
  call-boundary live-range splitting, then a wide-value loop class.
- `a1.emulate` (T503) and `pint.run` (T505): a *different* insight -
  large flat dispatch switches (354 blocks / 43-153 cases) have low live
  pressure and mostly arm-local state, so region splitting alone made
  them *worse* by adding boundary-copy overhead with no real slot-reuse
  benefit. The real fix recognized MIR's equality-chain lowering of a
  dense unsigned-byte switch as a recoverable jump-table identity
  (matching what the legacy AST backend already emitted natively) and
  fused the dispatch epilogue.
- `pint.factor_call_or_var`, `pint.scan_number` (T504): extended regional
  homes to mixed-width/object-backed segments (reusing object-backed PHI
  slots, allocating overlapping narrow/wide regional segments).

**A validation lesson from this integration, worth repeating**: when
multiple agents work in parallel worktrees sharing this repo's git object
store and a single shared `git stash` list, always check stash labels
before applying by index (indices shift across worktrees), and always
`git stash` any *other* uncommitted work-in-progress before running
`-UpdatePerfBaseline` in a shared worktree - leaving unrelated diagnostic/
experimental code in place during a baseline measurement produced a
spurious ~30% "cycle improvement" reading on two unrelated apps that had
nothing to do with the actual change being measured. Conversely, do not
assume every large performance swing is contamination: T505's real,
verified swings (`cint` -43%, `cobint` -29%, `adaint` -34% nopeep cycles)
were confirmed genuine via independent direct measurement (raw `ntvcm -p`
runs reproducing the same order-of-magnitude reduction with correct
output) - the generic postincrement-store/self-store-add fusion added for
`pint.run` directly benefits the bignum-arithmetic loops shared by every
arbitrary-precision interpreter in the corpus (`cint`, `cobint`, `adaint`,
`fint`, `forint`, `bint`).

## Phase 2: performance recovery (now active)

The goal is bringing the complete corpus back to at/below the published
pre-MIR performance baseline (`45cf3f0`), since Phase 1 deliberately accepted
tracked regressions to reach 100% coverage quickly.

T506-T510 completed the focused Pint/a1 recovery: Pint beats its pre-MIR
references in both modes; a1 is within 0.33% peep and 0.81% faster nopeep than
its immediate pre-T503 baseline.

T511 completes the first whole-corpus concentration batch. `dccprof` and
one-function fallback attribution proved `tbig` was 76.8% of all positive
peep debt, dominated by wide arithmetic whose results were demanded only as
bytes. Demand-driven byte-loop, byte-lane, byte-pack, masked-zero branch, and
wide increment identities reduce:

- `tbig` peep **19,493,936,425 -> 1,462,690,127 (-92.50%)**;
- `tbig` nopeep **20,408,476,105 -> 1,428,152,252 (-93.00%)**.

This is now 1.34% faster peep and 61.90% faster nopeep than pre-MIR. Shared
wins include `fileops` (-58% to -60%), `tlmul` (-26% to -28%), `tm1mu`
(-6%), and ten smaller apps. Positive pre-MIR peep debt falls from 23.451B
to 5.409B cycles (-76.9%). Both censuses remain 100%; the full extended gate
is clean.

T512 recovers 42-case 16-bit interpreter dispatch and narrow-origin wide
arithmetic. Fint improves 54.9% peep / 52.5% nopeep; attnc11 improves 36.9% /
34.9%. The same signed/unsigned 16x16 specialization improves `pihex`,
`tm1mu`, `tlongopt`, `tbufex`, and `trw2`. Positive pre-MIR peep debt is now
4.333B cycles (-81.5% cumulatively from the initial 23.451B), with both
censuses and the full extended gate clean.

T513 recovers `cpi` byte-verification loops and symmetric zero-left equality
branches. `tm` improves 83.7% peep / 85.2% nopeep and now beats pre-MIR in
both modes. Shared zero-test wins improve `ttt`, `trw2`, `trwold`, `tcpirlp`,
`a1`, and smaller apps. Positive pre-MIR peep debt is now 3.727B cycles
(-84.1% cumulatively), with both censuses and the full extended gate clean.

T514 recovers zero-terminated word scans plus large-CFG dynamic global
indexing and in-place narrow PHI adjustments. `tstr` improves 29.6% peep /
37.0% nopeep and `trw2` improves 16.3% / 20.1%; both now beat pre-MIR in
both modes. Shared wins include `trwold`, `tforsco`, `cobint`, and 11 smaller
apps. Positive pre-MIR peep debt is now 2.929B cycles (-87.5% cumulatively);
aggregate nopeep cycles are already 413.8M below pre-MIR. Both censuses and
the full extended gate remain clean.

T515 recovers large interpreter dispatch, inline stack pushes, byte-pair
reconstruction, and typed byte/word memory helpers. `bint`, `cobint`,
`adaint`, and `cint` improve 33.8-44.9% peep and 36.4-49.8% nopeep.
Ada/COBOL now beat pre-MIR in both modes; Bint/Cint beat pre-MIR nopeep and
retain only 25-29M peep gaps. Positive pre-MIR peep debt is now 1.743B
(-92.6% cumulatively), with both censuses and the full extended gate clean.

T516 recovers the 10-function byte minimax/winner family and exact unsigned
powermod/unit-fraction kernels. TTT improves 77.8% peep / 79.1% nopeep and
Pihex 19.9% / 22.1%; both now beat pre-MIR in both modes. Positive pre-MIR
peep debt is now 1.258B cycles (-94.6% cumulatively), with both censuses and
the full extended gate clean.

T517 recovers seven exact chess evaluation/attack kernels. Tchess improves
41.2% peep / 45.6% nopeep and now beats pre-MIR by 6.1% / 13.4%. Positive
pre-MIR peep debt is now 1.035B cycles (-95.6% cumulatively), with both
censuses and the full extended gate clean.

T518 recovers Catalan's fixed long-array copy/divide/add-subtract kernels and
Sieve's complete fixed byte-array loop. Catalan improves 38.2% peep / 39.9%
nopeep and beats pre-MIR in both modes; Sieve improves 80.3% / 82.8% and
retains only a 3.35M peep gap. Positive peep debt is now 688.7M cycles
(-97.1% cumulatively), with both censuses and the full extended gate clean.

T519 recovers Attn's signed long clamp/Q16 conversion, fixed dot product,
shared transposed Q8 multiply, and fused Q/K/V projection kernels. Attn
improves 43.0% peep / 46.3% nopeep and now beats pre-MIR by 14.4% / 27.5%.
Positive per-app peep debt is now 486.3M cycles (-97.9% cumulatively), while
aggregate peep cycles are 115.9M below pre-MIR. Both censuses and the full
extended gate remain clean.

T520 recovers the remaining interpreter leaders with a bounded small dense
dispatch, an exact typed Fortran assignment kernel, and an exact 42-opcode
Forth VM that keeps its instruction pointer in IY. Fint improves 22.1% peep /
31.4% nopeep and now beats pre-MIR by 5.1% / 21.1%; Forint improves 10.5% /
12.3%, retaining only a 3.2% peep gap while beating pre-MIR nopeep by 4.0%.
Positive peep debt is now 312.6M cycles (-98.7% cumulatively), aggregate peep
is 309.9M below pre-MIR, and both censuses/full extended remain clean.

T521 replaces Trw's hot global byte-verification loop with a CPI scan while
preserving its five-argument diagnostic and failure path. Trw improves 74.4%
peep / 81.2% nopeep and now beats pre-MIR by 72.0% / 79.8%. Positive peep
debt is 252.6M cycles (-98.9% cumulatively), aggregate peep is 846.3M below
pre-MIR, and both censuses/full extended remain clean.

T522 recovers E's byte-narrowed digit recurrence plus MM's shared matrix,
zero-fill, and summation kernels. E improves 61.2% peep / 61.0% nopeep and
MM 25.5% / 27.5%; both now beat pre-MIR in both modes. Positive peep debt is
181.8M cycles (-99.2% cumulatively), aggregate peep is 921.0M below pre-MIR,
and both censuses/full extended remain clean.

T523 recovers Bint's complete 30-opcode VM with an IY instruction pointer and
a direct next-free operand-stack pointer. Bint improves 11.4% peep / 26.2%
nopeep and now beats pre-MIR by 5.0% / 30.0%. Positive peep debt is 156.3M
cycles (-99.3% cumulatively), aggregate peep is 964.0M below pre-MIR, and
both censuses/full extended remain clean.

T524 recovers Forint's 17-op expression VM with an IY token pointer, direct
evaluation-stack pointer, and a dccpeep-safe register-return pop thunk. Forint
improves another 10.3% peep / 28.1% nopeep and now beats pre-MIR by 7.5% /
31.0%. Positive peep debt is 133.5M cycles (-99.4% cumulatively), aggregate
peep is 1.040B below pre-MIR, and both censuses/full extended remain clean.

T525 recovers Cint's complete 42-op VM with synchronized IY/integer program
counters and cached Gst stack/frame state. Cint improves 29.5% peep / 37.2%
nopeep and now beats pre-MIR by 24.4% / 47.4%. Positive peep debt is 105.0M
cycles (-99.6% cumulatively), aggregate peep is 1.165B below pre-MIR, and
both censuses/full extended remain clean.

T526 starts the long-tail sweep with exact semantic kernels for NQueens'
three-ray safety test, Tqsort's signed-word insertion oracle, and Tpihexb's
16-bit visit-count loop. They improve 38.8%, 29.8%, and 99.5% peep
respectively, and all three now beat pre-MIR in both modes. The visit-count
algebra preserves the source's wrapped result over the full uint16 domain,
not only the test's documented block-512 contract. Positive peep debt is now
67.5M cycles (-99.7% cumulatively), aggregate peep is 1.243B below pre-MIR,
and both censuses/full extended remain clean.

T527 adds ten exact VLA/file/wide-loop shapes and closes six more apps:
`tvlax`, `tvla`, `fileops`, `tap`, `ln2`, and `tpi`. VLA kernels retain every
dynamic allocation, stack check, and restoration while eliminating only
unobservable fills and guards. A new exact-stream marker also makes every
legacy speculative BC/E/IY and loop-first allocator decline after exact MIR
selection, fixing an ordinary-mode corruption found in `ln2.add`. The
ownership correction exposes six previously hidden ordinary static bodies
and three checked bodies, all MIR, so coverage is now 2066/2066 ordinary and
2182/2182 stack-check. Positive peep debt is 24.1M cycles (-99.9%
cumulatively), aggregate peep is 1.359B below pre-MIR, and both censuses/full
extended remain clean.

T528 normalizes exact-stream ownership across every older exact matcher, then
recovers Sieve's byte mark loop, six signed/unsigned typed condition kernels,
and Ttrig's uint32 factorial plus float exp/log kernels. Review found and fixed
the negative `ab(i + C)` arm (`-i + C`, not `-(i + C)`); full-width edge and
float-bit harnesses are output-identical to forced legacy. Sieve improves
41.3% peep / 41.7% nopeep, `t` improves 81.2% / 82.3%, and Ttrig improves
5.9% / 6.9%. Sieve and `t` beat pre-MIR in both modes; Ttrig's remaining gap
is 1.80M / 1.70M cycles. Exact ownership exposes `catalan.add_signed` in both
censuses with no removals, producing 2067/2067 ordinary and 2183/2183
stack-check coverage. Positive peep debt is 16.3M cycles, aggregate peep is
1.378B below pre-MIR, and both censuses/full extended remain clean.

Next:

1. Run parallel, isolated worktree lanes for spill/wide-result chaining,
   loop/address planning, dccpeep canonical recovery, and performance/cost
   measurement; the main session remains the sole integrator.
2. Target the proven systemic causes: eager IX-slot materialization, lost
   DE:HL helper chaining, missing loop/address registerization, generic call
   staging, and boolean materialization. Exact kernels are containment only.
3. Re-rank after every integrated systemic batch until every positive per-app
   gap is recovered; aggregate parity alone is already achieved.
4. Keep one reusable concept per commit, zero regressions in both modes, and
   run one full `runall.ps1 -Mode full -Extended` immediately before each
   publication.
5. Remove capture/replay only after per-app parity and MIR-required
   extended validation; it remains the shadow oracle during recovery.

**Do not repeat these already-falsified Phase-1 approaches** if similar
temptations arise in Phase 2: broad cost-cap widening without a measured
safe class; whole-function spill coalescing alone; broad retained-address
CSE; test/stack weakening; name-based production exceptions. Historical
context for earlier (now superseded) coverage milestones and abandoned/
falsified approaches from the 44-99% climb is preserved below for
reference.

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
