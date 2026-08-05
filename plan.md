# dcc MIR migration - current handoff

`mir-text-size-plan.md` is the authoritative migration log. Historical plans
were removed after their completed findings were folded into that log; git
history preserves them.

## Current state

- Branch: `perf/unified-regalloc`
- Published baseline: `69675cb` (Items T179-T180)
- Published ordinary coverage: **696/2022 functions (34.42%)**
- Published stack-check coverage: **703/2124 functions (33.10%)**
- Batch 9 ordinary coverage: **605/2021 functions (29.94%)**
- Batch 9 stack-check coverage: **610/2123 functions (28.73%)**
- Batch 10 ordinary coverage: **608/2021 functions (30.08%)**
- Batch 10 stack-check coverage: **614/2123 functions (28.92%)**
- Batch 11 ordinary coverage: **615/2021 functions (30.43%)**
- Batch 11 stack-check coverage: **621/2123 functions (29.25%)**
- Batch 20 candidate ordinary coverage: **704/2022 functions (34.82%)**
- Batch 20 candidate stack-check coverage: **713/2124 functions (33.57%)**
- Dominant fallback: `text-size` through `spilled-scalar-cfg`
- Goal: 100% MIR emitter coverage without correctness or peep/nopeep
  performance regressions

## Batch 2

1. T87: remove superseded planning documents and refresh this handoff.
2. T88: consolidate duplicated dead-store-forwarding success accounting.
3. T89: completed the safety audit and diagnostic census (402 unique
   function/object opportunities across 88 apps).
4. T90-T91: completed safe suffix trimming and the remaining profitability
   sweep; coverage is 531/2024 (26.24%), with seven new MIR functions and no
   focused peep/nopeep regressions.
5. T92: profile the remaining comparator and near-cost candidates; retain
   fallback for mixed/regressing results.
6. T93: add selected-assembly hashes to census snapshots.
7. T94: test and reject EXTRN-neutral size accounting after a peep regression.
8. T95: admit the structurally proven dead-suffix instruction win in
   `tcnstfld.main`; coverage reaches 532/2024.
9. T96: schedule fallback hash changes for runtime validation and establish a
   deterministic stack-check census.
10. T97: add a measured dead-suffix profitability floor after the
    CI-equivalent gate exposed four weak-margin regressions.

Batch 2 contains eleven evidence-backed items, T87-T97.

## Batch 3

1. T98: audit constant absolute-address chains: 143 functions and 1,188
   eligible accesses, including 99 text-size fallbacks.
2. T99: add one shared resolver and direct one-/two-byte absolute loads and
   stores, including safe intermediate-chain removal and HL forwarding.
3. T100: extend the resolver to fixed-stride constant indexes, with a
   four-percent instruction-margin gate after two measured regressions.
4. T101: implement, measure, and fully revert 32-bit absolute accesses after
   every newly admitted function regressed peep speed or linked size.
5. T102: sweep refreshed near misses and admit only
   `tpeepal.global_escape_store` under a slotless two-block structural gate.
6. T103: reproduce five CI-only performance regressions with the exact
   upstream ntvcm revision and add a 6% byte-and-instruction margin for
   member-only absolute addressing.

The exact-source ordinary census is 534/2024 and stack-check census is
537/2125, both +5 with zero removals. The focused and mandatory full+extended
CI-equivalent gates pass with zero checked regressions.

## Batch 4

1. T104: retain direct DE:HL-to-stack handoff for arithmetic wide helpers;
   comparison helpers remain excluded after a measured nopeep regression.
2. T105: admit pointer parameters only after a per-reference classifier proves
   every use is a dereference/index/member/comparison/direct return and rejects
   forwarding, calls, stores, arithmetic, and address-taking; all scalar MIR
   comparison paths treat pointer ordering as unsigned.
3. T106: reserve real local storage in homed prologues, fix the latent
   parameter-offset bugs for both direct and named accesses when IY is saved,
   and require reachable local/parameter accesses to establish IX.
4. T107: add conservative unpromoted local/global/extern word stores, exclude
   parameter stores, arbitrate against the spilled selector, and gate on a
   measured instruction/size margin.
5. T108: add one- and two-byte named memory operations and byte parameters
   with correct signed, unsigned, and `_Bool` normalization; retain existing
   word-indirect support but remove the byte-indirect experiment after a
   measured `tpeepal` regression.
6. T109: admit single-wide-value long parameters in HL:DE; this mostly improves
   already-MIR functions rather than adding names.
7. T110: share the bounded constant-multiply policy/emitter between spilled and
   homed backends; homed multiplication and constant shifts preserve live DE
   explicitly.
8. T111: remove the zero-yield byte-return experiment after the exact-CI gate
   exposed a `ts` regression; preserve frameless wide constants while requiring
   IX only for used wide parameters, and share NOP/label-only jump-fallthrough
   detection between both CFG emitters.
9. T112: measure and reject zero-impact wide identity-cast, word-return, and
   dynamic-index experiments; the latter also exposed and removed an unsafe
   copied-instruction/liveness-index prototype.

Current censuses are **559/2022 ordinary** and **564/2123 stack-check**,
respectively +25 and +27 accepted names from the published T103 baseline with
zero removals. The denominator fell by two because `a1.end_emulation` and
`a1.soft_reset` were fallback-only functions that are now eliminated by inline
retention; no accepted function disappeared. Exact-CI focused validation of
`tpeepal`, `tlongopt`, and `ts` passes after the final tightening.

## Batch 5

1. T113: correct phi discovery so labels terminate a block after substantive
   instructions, preventing copies from being attached to a later block.
2. T114: retain measured fallback gates for one-call boolean phis and large
   call/phi CFGs.
3. T115: allocate two independent wide homes, `HL:DE` and `BC:IY`, while
   preserving the IY callee-save and parameter-offset ABI.
4. T116: centralize wide pair moves, constants, stack handoff, and cast
   emission for reuse by homed and spilled backends.
5. T117: support wide parameters and integer casts, negation, and complement.
6. T118: support non-helper wide arithmetic and direct constant
   add/subtract/bitwise/shift/power-of-two multiplication.
7. T119: arbitrate wide homed output against spilled output; simple pair-homed
   functions that lose to stack evaluation remain fallback.
8. T120: admit the measured single-block indirect read-modify-write class.
9. T121: admit the measured pointer-member picker class.
10. T122: admit the masked-`memset` wrapper class only when the `& 255` result
    is the matching call site's fill-byte argument.

The ordinary census is **570/2022 (28.19%)**, +11 names from T112 with zero
removals. The stack-check census is **576/2124 (27.12%)**, +12 names with zero
removals. Newly emitted ordinary functions are `tgoto.gt_forward`,
`tgoto.gt_multi_label`, `tgoto.gt_out_block`, `thoistbc.main`,
`tvla.fixed_cast_bounds`, `tclit.add_two_long`, `tctxops.ca_sink`,
`tinlinfb.store_add`, `tptrcnd.pickip`, `tptrrhs.pickip`, and `trw.fill_buf`.
The final affected-app full-mode check has zero regressions.

## Batch 6

1. T123: preserve only scalar comparison homes that are live across the
   comparison, including overlapping `HL:DE` homes; retain a structural
   call-heavy profitability gate for the three measured regressions.
2. T124: measure and reject representation-only float parameters, constants,
   and returns after they add no names and change eight apps without benefit.
3. T125: measure and reject broad repeated-comparison admission after one
   miscompile, five regressions, and fallback-only hash changes expose
   non-transactional speculative selector state.
4. T126-T127: add 32-bit indirect loads and stores using the shared wide-pair
   contract and physical-register-overlap-aware preservation.
5. T128-T129: measure and remove byte indirect loads/stores after exact-CI A/B
   confirms the newly admitted function regresses peep cycles.
6. T130-T131: centralize bitfield mask/extraction helpers and reuse them for
   homed indirect bitfield loads and read-modify-write stores.
7. T132: add bounded constant-stride dynamic indexing, save the base before
   scaling, preserve overlapping scalar and `HL:DE` homes, always arbitrate
   this class against the spilled selector, and require an instruction-count
   win after exact-CI A/B rejects the equal-count two-block candidate.

The ordinary census is **573/2022 (28.34%)**, +3 names from T122 with zero
removals. The stack-check census is **579/2124 (27.26%)**, also +3 with zero
removals. Both add `adaint.acc`, `adaint.need`, and `pint.statement`. Dynamic
indexing also replaces the existing spilled selection for `t2denum.main` with
faster homed output. The 11-app peep/nopeep focused run passes with zero
regressions.

## Batch 7

1. T133: measure and revert rematerializable-wide-constant coloring after it
   moves two functions past the selector gate but leaves both roughly twice
   the legacy instruction count.
2. T134: move constant absolute-address resolution, chain validation, and
   operand formatting from the spilled backend into the shared emitter module;
   reuse it to fold homed global/member/constant-index word accesses and omit
   their now-dead address chains.
3. T135: retain the call-heavy comparison gate generally, but allow the
   measured direct-absolute-dependent, at-most-four-block shape. Broader
   candidates either miscompile, regress, or reach the separate backedge gate.
4. T136: add direct byte absolute stores only for single-block functions.
   Exact-upstream validation rejected byte loads and larger byte-access CFGs
   after `tbool` and `tlongidx` showed three cycle regressions.

The ordinary census is **583/2021 (28.85%)**, +10 accepted names and zero
accepted removals. The stack-check census is **589/2123 (27.74%)**, also +10
with zero accepted removals. The one-row denominator reduction is the legacy
fallback `tcodegen.scnt`, which is no longer reported after the surrounding
speculative-codegen choice changes; it is not a lost MIR function. Additions
in both configurations are `cint.acc`, `cint.expr_stmt`, `cint.need`,
`cint.return_stmt`, `cint.statement`, `cobint.stmt_for_para_i`,
`cobint.tpeek`, `tc99init.main`, `tcodegen.scod`, and `tcodegen.srdy`.

The mandatory full+extended gate passed against upstream ntvcm
`e47c9cd34b7d309b7a1d8e7c4329e7672c0e9c9f`: 314 runnable apps, diagnostics,
dccpeep fixtures, extended tests, and both performance modes passed with zero
regressions.

## Batch 8

1. T137: include constant-absolute-dependent homed output in the existing
   homed-versus-spilled arbitration; the guard is behavior-neutral today and
   prevents future absolute-address extensions from bypassing comparison.
2. T138: extend stable incoming-parameter forwarding to signed, unsigned, and
   `_Bool` bytes, including correct IX/IY and out-of-range normalization.
3. T139: retain fallback for multi-block byte-parameter candidates unless
   direct forwarding produces a real instruction win; exact-upstream A/B
   rejected `tptrlhs.check_char` and `tptrrhs.check_char`.
4. T140: admit the measured strong instruction-win text-proxy and compact
   homed shapes with structural thresholds narrowed for VLA and chained-CFG
   hazards.

The ordinary census is **593/2021 (29.34%)**, +10 names and zero removals.
The stack-check census is **598/2123 (28.17%)**, +9 names and zero removals;
`tbug.swdf` remains below the stack-check profitability threshold. Ordinary
additions are `attnc11.transfer_weight_group`, `t.si8`, `t.sui8`,
`tbug.swdf`, `tc89swjt.swsp`, `tcrcfix.call_cleanup_callee`, `ts.shi8`,
`ts.shui8`, `tvla.unused_vla_prune_same_decl`, and
`tvla.unused_vla_prune_sp_alias`.

The mandatory full+extended gate passed against upstream ntvcm
`e47c9cd34b7d309b7a1d8e7c4329e7672c0e9c9f` with zero regressions.

## Batch 9

1. T141: parameterize constant-absolute chain-use traversal by the current
   emitter's final-access policy; the spilled backend's broader byte policy
   must not remove addresses still required by homed emission.
2. T142: add one reusable narrow scalar spill slot to homed emission, including
   frame allocation and shared load/store/push/constant/address/parameter
   helpers. Reject wide and phi-connected spills and require a smaller,
   no-more-instructions candidate with at most four blocks.
3. T143: admit ABI-compatible pointer returns, including `void *` value
   returns. Character, `_Bool`, float, and struct-object returns remain
   excluded until their full normalization contracts are represented.
4. T144: remove zero-offset same-home pointer shuffles and avoid preserving
   dead HL while loading a frameless pointer parameter into DE/BC. Scalar
   parameter output remains stable because broad save elision changed
   speculative inline-retention reporting.

The ordinary census is **605/2021 (29.94%)**, +12 names and zero removals.
The stack-check census is **610/2123 (28.73%)**, also +12 with zero removals.
Both add `pint.if_stmt`, `tbsearch.cmp_rec`, `texstrct.by_name`, `too.op_set`,
`tptrcnd.pickcp`, `tptrcnd.pickl`, `tptrcnd.picklp`, `tptrrhs.pickcp`,
`tptrrhs.pickl`, `tptrrhs.picklp`, `tqsort.cmp_rec`, and `wumpus.rmove`.

Rejected experiments include constrained-order recoloring (zero coverage
change and severe compile-time cost), wide call arguments (zero coverage
change), generic byte-indirect loads (one peep regression), and character
returns (incomplete byte-arithmetic normalization). A bug-focused review
corrected `void *` return semantics and tightened byte-conversion proofs. The
affected full-mode set passes with zero regressions.

The mandatory full+extended gate passed against upstream ntvcm
`e47c9cd34b7d309b7a1d8e7c4329e7672c0e9c9f`: 314 runnable apps, diagnostics,
dccpeep fixtures, extended tests, and both performance modes passed with zero
regressions.

## Batch 10

1. T145: convert deferred `MIR_OBJECT_MERGE` placeholders to named loads when
   pointer-parameter filtering removes their object metadata. This eliminates
   the stale `selector` rejection bucket without changing selected output.
2. T146: make boundary-register preservation use the verifier's retained
   edge-aware liveness instead of a textual future-use approximation. Values
   from mutually exclusive branches no longer cause redundant stack saves.
3. T147: admit the measured no-phi, at-most-18-block repeated-comparison slice
   after the liveness fix; phi-bearing and larger loaded CFGs remain excluded.
4. T148: compare both homed and spilled output for that repeated-comparison
   slice and retain spilled output when the homed call-heavy profitability gate
   would otherwise discard an already-profitable migration.

The ordinary census is **608/2021 (30.08%)**, +3 names and zero removals.
The stack-check census is **614/2123 (28.92%)**, +4 names and zero removals.
Ordinary additions are `attnc11.process_sequence`, `bint.relation`, and
`pint.parse_expr`; stack checking also adds `fint.op_has_local_target`.

The five-app focused full-mode run passes with zero regressions. Edge-aware
liveness also fixes two of the three historical forced backedge
miscompilations, but the gate remains intact: `adaint.var_or_const_decl`
still miscompiles and `bint.sum` still grows linked peep size.

The mandatory full+extended gate passed against upstream ntvcm
`e47c9cd34b7d309b7a1d8e7c4329e7672c0e9c9f`: 314 runnable apps, diagnostics,
dccpeep fixtures, extended tests, and both performance modes passed with zero
regressions.

## Batch 11

1. T149: omit dead backend slots for float `MIR_BINARY` results forwarded
   directly to `MIR_RETURN`; float unary/conversion results remain excluded
   after `tctxflt.tf_ret` regressed both modes.
2. T150: add actual slot-access diagnostics and stop reserving slots for
   values defined by `MIR_NOP`. This changes no coverage row but materially
   improves several already-MIR apps.
3. T151: reuse `mir_address_is_single_call_argument()` in slot preparation so
   rematerialized local/parameter addresses no longer reserve unreachable
   frame space.
4. T152: retire duplicate-epilogue size compensation only for the measured
   no-phi, multi-block, non-worse byte/instruction class. Broader removal
   admitted 14 functions but regressed 13 app/mode measurements; the retained
   slice adds four clean functions.

The ordinary census is **615/2021 (30.43%)**, +7 names and zero removals:
`cint.find_sym`, `pihex.eps`, `tchess.ch_bk_move`, `tcodegen.tchk2`,
`tfloat4.add3`, `tfloat4.muladd`, and `tgoto.gt_switch`.

The stack-check census is **621/2123 (29.25%)**, also +7 names and zero
removals: `pihex.eps`, `tbug.swdf`, `tbug.swft`, `tchess.ch_bk_move`,
`tfloat4.add3`, `tfloat4.muladd`, and `tgoto.gt_switch`.

Rejected experiments include broad float unary/conversion slot elision,
float constant/call return expansion (zero yield), simulated call-cache slot
elision (18 accepted removals), and broad epilogue-compensation retirement
(13 checked regressions). The exact affected full-mode sets pass with zero
regressions.

The mandatory full+extended gate passed against upstream ntvcm
`e47c9cd34b7d309b7a1d8e7c4329e7672c0e9c9f`: 314 runnable apps, diagnostics,
dccpeep fixtures, extended tests, and both performance modes passed with zero
regressions.

## Batch 12

1. T153: reuse the emitter's scalar stack-forwarding predicates during backend
   slot preparation. Values consumed directly by index or binary handoff no
   longer reserve unreachable frame slots. The shared predicate rejects
   `MIR_PHI`: predecessor emission writes phi destinations before the linear
   phi instruction, so they require real storage.
2. T154: admit constant-absolute candidates outside the distinct two-block
   peephole class when generated bytes and instructions are both no worse than
   legacy. Forced full-mode A/B covered the complete affected population;
   `cobint.emit_tok` proves the two-block boundary remains necessary.
3. T155: audit larger text-proxy instruction wins before changing that gate.
   Six of seven profiled functions regressed linked size, runtime, or
   correctness; only `00040b.main` was neutral/positive. No broad gate change
   was justified.

The ordinary census is **617/2021 (30.53%)**, +2 names and zero removals:
`a1.m_hook` and `cint.init_compile_storage`.

The stack-check census is **625/2123 (29.44%)**, +4 names and zero removals:
`a1.m_hook`, `cint.find_sym`, `cint.init_compile_storage`, and
`tcodegen.tchk2`.

The ten affected apps pass focused full-mode validation with zero regressions
and 20 checked cycle/size improvements.

## Batch 13

1. T156: enrich the opt-in actual slot-access diagnostic with use count, first
   consumer, and definition-to-consumer distance. The refreshed report showed
   4,722 genuinely unused assigned slots; call-argument caching dominated.
2. T157: factor call-cache target discovery into a state-parameterized shared
   predicate and plan its choice in the same definition order as emission.
   Cache-only values use a distinct backend-slot state instead of consuming
   frame space. PHI, entry-parameter, fused divmod, and odd aggregate-argument
   call-result shapes retain storage because their emission is not a single
   linear store.
3. T158: apply the exact plan to both the narrow BC cache and wide alternate
   register cache. A cacheable span permits no intervening value-producing
   instruction, so the two cache lifetimes cannot overlap. The post-change
   diagnostic has no unused narrow slots and reduces total unused assigned
   slots from 4,722 to 1,238.

The ordinary census is **621/2021 (30.73%)**, +4 names and zero removals:
`tptrlhs.check_char`, `tptrrhs.check_char`, `tunary.shi32`, and
`tunary.shui32`.

The stack-check census is **628/2123 (29.58%)**, +3 names and zero removals:
`tptrlhs.check_char`, `tptrrhs.check_char`, and `tunary.shi32`.

The 19 affected apps pass focused full-mode validation with zero regressions
and 42 checked cycle/size improvements.

## Batch 14

1. T159: use retained CFG liveness for unary and constant-binary home
   preservation. Applying the same rule to general binary emission admitted
   `tasmcoll.main` but regressed peep cycles, so that broader experiment was
   removed.
2. T160: use retained liveness for pointer-offset addressing and for IX-offset
   addressing in multi-block CFGs. Straight-line IX emission remains stable
   after broader liveness admitted a slower `tstr2.test_strcat`. The retained
   slice adds `pint.factor`.
3. T161: support up to four narrow homed spill slots under the existing
   at-most-four-block, smaller-text, no-more-instructions gate. This improves
   `tdecinit`; widening to sixteen changed nothing further.
4. T162: rematerialize scalar constants in single-block and VLA functions and
   immutable string labels generally. The scalar slice adds four functions;
   broad multi-block scalar rematerialization was rejected after multiple
   runtime/size regressions.
5. T163: rematerialize single-block 32-bit integer and float constants directly
   into DE:HL. Candidates that depend on this optimization reject calls that
   require hexadecimal or octal formatter runtime helpers and require strictly
   smaller generated text. Precise dependency tracking covers multiply
   consumers as well as ordinary wide loads.

The candidate ordinary census is **638/2021 (31.57%)**, +17 names and zero
removals. The candidate stack-check census is **645/2123 (30.38%)**, also +17
with zero removals. The common additions are `pint.factor`,
`tc89comp.cai1`, `tforblk.static_shadows_auto`, `tlong.tasgn`,
`tlong.tbasic`, `tlong.tcomp`, `tlong.tglob`, `tlong.tneg`,
`tlongreg.test_compound`, `tlongreg.test_postfix`,
`tlongreg.test_shifts`, `tpfinf.main`, `tpflio.main`, `tplng.main`,
`tpromo.test_function_arguments_and_returns`,
`tpromo.test_usual_arithmetic_conversions`, and
`tvla.vla_sizeof_ternary`.

Rejected wide experiments include ungated rematerialization, which exposed a
formatter-runtime correctness failure and slower tiny long helpers, and
multi-block rematerialization, whose three additions regressed `mm` and
`tlongopt`. Focused full-mode validation of the retained slices passes with
zero regressions and substantial peep/nopeep improvements.

## Batch 15

1. T164: test paired boolean-result stack forwarding on the complete 15-function
   `okb` population. Every function was correctness-clean and improved nopeep,
   but every peep build regressed. The implementation and fixture were removed.
2. T165: fuse a narrow logical-not used once by an immediately following
   false branch. The spilled backend tests the source truth value directly,
   elides the boolean result slot, and reuses the existing conditional
   branch/phi-copy emitter. Production excludes a directly consumed variadic
   call result, nested unary sources, candidates over 18 blocks, and candidates
   without a ten-byte generated-size margin. Broad admission exposed 27
   functions but regressed `tvariad`, `ttrig`, `cobint`, and `forint`; the
   measured retained class adds 16 with zero removals. Wide-source fusion
   changed no accepted function and was removed.
3. T166: record actual variadic prototypes on MIR calls with dedicated flag
   4096. Existing flags 32 and 64 mean that hexadecimal or octal formatter
   runtime helpers are required; homed rejection and the established
   wide-rematerialization gate now use formatter-runtime terminology.

The ordinary census is **654/2021 (32.36%)**, +16 names and zero removals.
The stack-check census is **661/2123 (31.14%)**, also +16 names and zero
removals. Both configurations add `adaint.need_word`, `adaint.xcalloc`,
`bint.need`, `bint.xcalloc`, `cint.init_state`, `cint.xcalloc`,
`cobint.xcalloc`, `cpmenumd.main`, `fint.xcalloc`, `forint.xcalloc`,
`pint.xcalloc`, `tc89flng.chk`, `tc89ini2.ck`, `tcodegen.tchk3`,
`tctype.chk_int`, and `tvlax.ok`.

The affected-app full peep/nopeep run passes with zero regressions and 30
checked improvements.

## Batch 16

1. T167: extend zero-spill homed 4-byte values to conservative single-block
   float arithmetic, float constants, and representation-preserving float
   unary identity. The implementation reuses the established wide-pair and
   runtime-helper contracts rather than adding a parallel float emitter.
2. T168: add direct global/extern 4-byte named loads and stores for both long
   and float values, plus 4-byte float indirect loads and stores. Named memory
   uses the existing resolved type and pair-home preservation machinery.
3. T169: fuse the exact adjacent `c + a*b` float shape to `__fmaf` when the
   multiply is the addition's right operand and has one use. The spilled
   backend elides the intermediate multiply slot and emits one runtime call.
4. T170: measure broader FMA fusion and retain only the legacy-compatible
   orientation and adjacency. Symmetric `(a*b)+c` newly linked the sizeable
   `__fmaf` runtime block and regressed `tfloat4`; nonadjacent interval
   extension added no accepted functions.
5. T171: measure and remove zero-yield 16-to-32-bit multiply fusion, homed
   float comparisons, and float unary negation. Minimal float identity remains
   because it is required by `tc89flta.f_st`.

The ordinary census is **659/2022 (32.59%)**, +5 names and zero removals.
The stack-check census is **666/2124 (31.36%)**, also +5 names and zero
removals. Both configurations add `tc89flta.f_gv`, `tc89flta.f_st`,
`tfmadd.local_case`, `tfpspec.madd`, and
`tlongopt.ret_global_live_add`. The denominator increase is a reporting
consequence of `tc89flta.f_gv` becoming reportable, not a lost MIR function.

The affected-app full peep/nopeep run passes with zero regressions and 13
checked improvements.

## Batch 17

1. T172: profile the refreshed `text-size` population and reject multi-block
   wide binary/unary support, retained-home spill coalescing, and CFG-aware
   spilled-slot reuse after they add no corpus coverage.
2. T173: reject broad low-slot admission after it adds five functions but
   regresses stack-check apps; comparison-branch arbitration changes no
   selected function.
3. T174: revalidate near-cost forced acceptance sequentially. Concurrent
   outer `runall` processes produced misleading results; the sequential
   campaign confirms that broad text-proxy widening remains unsafe.
4. T175: rematerialize a one-use string literal directly at its final
   argument push, sharing one predicate across coloring, definition emission,
   and argument emission. Restrict the slice to argument zero and at most
   three CFG blocks so loading the label cannot clobber a pending HL argument
   and measured larger-CFG regressions remain fallback.
5. T176: require string-rematerializing homed candidates to stay within one
   instruction of legacy. The measured two-instruction-deficit narrow shifts
   regress peep speed; rejecting that homed output lets the profitable spilled
   alternative win instead.

The published ordinary census is **674/2022 (33.33%)**, +15 names and zero
removals. The published stack-check census is **681/2124 (32.06%)**, also +15
with zero removals. Both add `tbug.main`, `tbug2.main`, `tc89decl.main`,
`tc89swjt.main`, `tdead.poison`, `tmirfast.main`, `tmirfuse.main`,
`tmirslot.main`, `tphijoin.main`, `tponce.main`,
`tstr3.test_strcspn`, `tstr3.test_strspn`, `tunary.shi8`,
`tunary.shui8`, and `tvlax.main`.

The 49 affected apps pass focused full-mode validation with zero regressions
and 98 checked cycle/size improvements.

## Batch 18

1. T177: cross-profile all 826 remaining `text-size` fallbacks against homed
   rejection classes. Reject zero-yield wide-whitelist and wide-call-result
   experiments, and reject general homed multiply after its only new function
   regresses both output modes by more than two percent.
2. T178: reuse stable incoming parameter homes for objectless pointer loads
   used as aggregate copy/call destinations. Resolve the declared `SC_PARAM`
   offset through one shared helper, reject reassigned or address-taken
   parameters, and retain the existing slot path for other pointer arithmetic.
   The broader form added `tptrdiff.long_dist` but regressed peep/nopeep, so it
   was narrowed to the measured aggregate-address class.

The candidate ordinary census is **679/2022 (33.58%)**, +5 names and zero
removals. The candidate stack-check census is **686/2124 (32.30%)**, also +5
with zero removals. Both add `tstructv.assign_return_pair_ptr`,
`tstructv.copy_pair_ptr`, `tstructv.copy_wrap_ptr`,
`tstructv.fill_big_ptr`, and `tunion2.copy_through_pointer`. Focused
peep/nopeep validation passes with four checked cycle improvements.

## Batch 19

1. T179: force-profile 18 `text-size` fallbacks with strong MIR instruction
   wins and small text deficits. Static instruction count is not a reliable
   runtime proxy: 16 candidates fail correctness or peep/nopeep performance.
   Retain only two structural gates proven by full-mode A/B: slotless,
   at-most-two-block candidates within 20 text bytes, and one-block VLA
   candidates that save at least eight instructions within 20 text bytes.
2. T180: fuse the repeated assertion-helper shape
   `(param != 0) != (param != 0)` when its result is consumed by the
   immediately following false branch. The two inner booleans receive no
   backend slots or standalone code; the outer comparison branches directly
   on the stable narrow parameter values. One-use, zero-RHS, narrow-type,
   immediate-branch, and no-phi proofs keep the optimization structural and
   local.

The candidate ordinary census is **696/2022 (34.42%)**, +17 names and zero
removals. The candidate stack-check census is **703/2124 (33.10%)**, also +17
with zero removals. Both add `00040b.main`, `tvla.vla_sizeof_saved_once`, and
the 15 `okb` helpers in `tasinfsp`, `tatan2sp`, `tcmpq`, `texpfsp`, `tfdf`,
`tfloorsp`, `tfmaf`, `tfmodfsp`, `tfpraw`, `tfpspec`, `tfrexpsp`, `tisnan`,
`tlogfsp`, `tpowfsp`, and `tsqrtsp`.

The 19 affected apps pass focused full-mode validation with zero regressions
and 42 checked cycle/size improvements. The refreshed ordinary rejection
population is led by 804 `text-size`, 140 `unary-not-cost`, 69
`wide-constant-cost`, 62 `instruction-count`, 54 `absolute-address-cost`, 50
`absolute-index-cost`, and 46 `inline-substitution` functions.

## Batch 20

1. T181: audit the 804 remaining `text-size` fallbacks for values produced,
   followed only by MIR no-ops, and then consumed as dynamic fixed-stride
   index bases. The pattern occurs in 157 functions. Generalize the existing
   physical-stack handoff so slot planning and emission use one target
   predicate and track the exact consumer instruction.
2. T182: fix the pointer-depth bug exposed by forced validation of the wider
   population. Deferred repair must preserve an original pointer-to-pointer
   load when its address type has saturated at `TYPE_PTR2`; index stride
   selection must prefer a pointer-valued struct field over its enclosing
   struct symbol. This changes `Gst.strs[i]` from the incorrect byte stride to
   the required two-byte pointer stride.
3. T183: profile the 14 initially admitted functions in full peep/nopeep mode.
   Retain dynamic index-base forwarding for call-free functions and require a
   measured 15-instruction saving for call-containing functions. This rejects
   117 correct-but-unprofitable candidates without app/function-name
   exceptions and retains the eight non-regressing functions.

The candidate ordinary census is **704/2022 (34.82%)**, +8 names and zero
removals. The candidate stack-check census is **713/2124 (33.57%)**, +10 names
and zero removals. Ordinary additions are `adaint.patch`, `cint.add_func`,
`cint.patch`, `cobint.patch`, `cobint.var_get`, `pint.patch`,
`tnestfor.nz_ptr`, and `too.tile_at`; stack-check additionally adds
`tvla.vla_leading_const_bound` and `tvla.vla_parenthesized_bound`.

The seven affected apps pass focused full-mode validation with zero regressions
and 16 checked cycle/size improvements. The refreshed ordinary rejection
population is led by 690 `text-size`, 138 `unary-not-cost`, 117
`dynamic-index-base-cost`, 69 `wide-constant-cost`, 62 `instruction-count`,
49 `absolute-index-cost`, 47 `absolute-address-cost`, and 46
`inline-substitution` functions.

## Required process

- Identify the exact affected functions before widening any gate.
- Keep semantic and profitability gates separate.
- Validate changed/newly accepted apps in `-Mode full`.
- Compare both ordinary and `-fstack-check` census snapshots.
- Never update performance baselines to hide a regression.
- Before every commit, run:

  ```sh
  pwsh ./scripts/runall.ps1 -Mode full -Extended
  ```

- `runall.ps1` now defaults to failures-only output; no explicit
  `-FailuresOnly` flag is needed.
- Validate with the current `davidly/ntvcm` `main` revision used by CI.
- Confirm the resolved local `ntvcm` revision matches upstream before treating
  a performance run as the pre-commit gate.
- Push published batches to `origin` (`davidly/dcc`), not the fork.
- Leave unrelated `.vscode/settings.json` changes untouched.

## Deferred work

- `cfg-backedge`: separate correctness project. Edge-aware liveness fixed two
  historical forced miscompilations, but `adaint.var_or_const_decl` remains a
  confirmed miscompile and broad admission still causes performance losses.
- Same-block address/value CSE: blocked/deferred by Item T70's negative
  experiment; it lengthened live ranges, increased fixed moves/slots, and lost
  net coverage. Revisit only with a materially different liveness/cost model.
- Continue zero-spill-first: add helper-clobber-aware wide homes (including a
  safe second pair), then reduce the 204 homed candidates that already emit but
  lose the profitability comparison.

## Roadmap to 50%

Continue the approved phased roadmap rather than restarting prioritization:

1. **Phase 3 - expanded:** two-pair wide allocation, non-helper operations,
   constants, and wide homed-vs-spilled arbitration are complete.
   Rematerializable-wide-constant coloring was measured and rejected at T133.
   Revisit helper-crossing wide values only when diagnostics identify a real
   `cross-call` population; the current 23 `wide-color` rejects all report
   `cross-call=0`.
2. **Phase 4 - complete:** the conservative per-reference pointer classifier is
   active with zero removals.
3. **Next structural priorities by impact:** the post-T183 ordinary census is
   led by 690 `text-size`, 138 `unary-not-cost`, 117
   `dynamic-index-base-cost`, 69 `wide-constant-cost`, 62
   `instruction-count`, 49 `absolute-index-cost`, 47
   `absolute-address-cost`, and 46 `inline-substitution` fallbacks. Re-bucket
   `text-size` by repeated emitted pattern before choosing the next shared
   improvement. The cost populations are deliberately measured fallback, not
   gate-removal headroom.
4. Keep same-block CSE deferred unless a new liveness model removes Item T70's
   measured slot/move regression.
5. Re-run ordinary and stack-check censuses after each structural batch and
   sweep the newly exposed near-miss layer.
6. Repeat evidence-backed structural batches until ordinary coverage reaches
   at least **50%**, preserving zero correctness and peep/nopeep regressions.
