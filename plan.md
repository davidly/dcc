# dcc MIR migration - current handoff

`mir-text-size-plan.md` is the authoritative migration log. Historical plans
were removed after their completed findings were folded into that log; git
history preserves them.

## Current state

- Branch: `perf/unified-regalloc`
- Published baseline: `232a992` (Items T221-T223)
- Published ordinary coverage: **756/2022 functions (37.39%)**
- Published stack-check coverage: **771/2124 functions (36.30%)**
- Batch 9 ordinary coverage: **605/2021 functions (29.94%)**
- Batch 9 stack-check coverage: **610/2123 functions (28.73%)**
- Batch 10 ordinary coverage: **608/2021 functions (30.08%)**
- Batch 10 stack-check coverage: **614/2123 functions (28.92%)**
- Batch 11 ordinary coverage: **615/2021 functions (30.43%)**
- Batch 11 stack-check coverage: **621/2123 functions (29.25%)**
- Batch 20 candidate ordinary coverage: **704/2022 functions (34.82%)**
- Batch 20 candidate stack-check coverage: **713/2124 functions (33.57%)**
- Batch 21 candidate ordinary coverage: **711/2022 functions (35.16%)**
- Batch 21 candidate stack-check coverage: **720/2124 functions (33.90%)**
- Batch 22 candidate ordinary coverage: **719/2022 functions (35.56%)**
- Batch 22 candidate stack-check coverage: **728/2124 functions (34.27%)**
- Batch 23 candidate ordinary coverage: **723/2022 functions (35.76%)**
- Batch 23 candidate stack-check coverage: **732/2124 functions (34.46%)**
- Batch 24 candidate ordinary coverage: **729/2022 functions (36.05%)**
- Batch 24 candidate stack-check coverage: **743/2124 functions (34.98%)**
- Batch 25 candidate ordinary coverage: **734/2022 functions (36.30%)**
- Batch 25 candidate stack-check coverage: **748/2124 functions (35.22%)**
- Batch 26 candidate ordinary coverage: **737/2022 functions (36.45%)**
- Batch 26 candidate stack-check coverage: **752/2124 functions (35.40%)**
- Batch 27 candidate ordinary coverage: **739/2022 functions (36.55%)**
- Batch 27 candidate stack-check coverage: **754/2124 functions (35.50%)**
- Batch 28 candidate ordinary coverage: **741/2022 functions (36.65%)**
- Batch 28 candidate stack-check coverage: **756/2124 functions (35.59%)**
- Batch 29 candidate ordinary coverage: **744/2022 functions (36.80%)**
- Batch 29 candidate stack-check coverage: **759/2124 functions (35.73%)**
- Batch 30 candidate ordinary coverage: **745/2022 functions (36.84%)**
- Batch 30 candidate stack-check coverage: **760/2124 functions (35.78%)**
- Batch 31 candidate ordinary coverage: **752/2022 functions (37.19%)**
- Batch 31 candidate stack-check coverage: **767/2124 functions (36.11%)**
- Batch 32 candidate ordinary coverage: **754/2022 functions (37.29%)**
- Batch 32 candidate stack-check coverage: **769/2124 functions (36.21%)**
- Batch 33 candidate ordinary coverage: **756/2022 functions (37.39%)**
- Batch 33 candidate stack-check coverage: **771/2124 functions (36.30%)**
- Batch 34 candidate ordinary coverage: **764/2023 functions (37.77%)**
- Batch 34 candidate stack-check coverage: **781/2125 functions (36.75%)**
- Dominant fallback: `text-size` through `spilled-scalar-cfg`
- Goal: 100% MIR emitter coverage without correctness or peep/nopeep
  performance regressions

## Batch 26

1. T197-T198: measure and reject both out-of-line materialization and direct
   MIR substitution for legacy inline-only calls. The former regresses linked
   size; the latter duplicates rematerialized expressions and admits no
   function.
2. T199: measure and reject broad unary-not/large-CFG exceptions after forced
   peep/nopeep profiling exposes regressions or later safety gates.
3. T200: admit three structurally profiled format-call near-cost candidates:
   two-block wide-value CFGs within nine bytes/two instructions, and slotless
   at-most-four-block CFGs within nine bytes and no extra instructions.
4. T201: fix duplicate phi copies across consecutive label pseudo-edges with
   one shared homed/spilled fallthrough predicate.
5. T202: keep the phi correction fallback-only and transactional, after a
   global rollout perturbs incumbent output and exposes regressing candidates.
   Run it only after incumbent lazy/stable/specialized retries, require a
   ten-instruction win, and preserve every existing selected hash.
6. Replace the duplicated inline-substitution call-flag literal with
   `MIR_CALL_FLAG_INLINE_SUBSTITUTABLE`.

The ordinary census is **737/2022 (36.45%)**, +3 names and zero removals.
The stack-check census is **752/2124 (35.40%)**, +4 names and zero removals.
Ordinary additions are `tfloat4.check_float`, `trw.must_seek`, and
`tunaryp.chku`. The five-app stack-check focused full peep/nopeep run passes
with zero regressions and five checked cycle/size improvements.

## Batch 27

1. T203: audit the dominant spilled selector's assigned backend slots. Of
   5,194 slots in current `text-size` fallbacks, 700 one-use narrow values
   feed the immediately following binary's right operand; the pattern occurs
   in 143 functions.
2. T204: generalize the existing constant-left RHS physical-stack handoff to
   any single-use narrow RHS. Preserve planned-LHS nesting by popping the newer
   RHS first and the planned LHS second. A global rollout is rejected after
   changing 119 apps and producing 14 checked regressions.
3. T205: make generalized RHS forwarding a fallback-only fresh-stream retry.
   Seven functions initially cross the standard selector gates, but four
   regress peep execution or linked size. Retain only the structurally exact
   single-block pointer-index picker, sharing its recognizer with the existing
   pointer-member picker.

The ordinary census is **739/2022 (36.55%)**, +2 names and zero removals.
The stack-check census is **754/2124 (35.50%)**, also +2 and zero removals.
Both add `tptrcnd.pickw` and `tptrrhs.pickw`; focused full peep/nopeep
validation passes with zero regressions and six checked improvements.

## Batch 28

1. T206: audit the next highest-frequency active slot class. The current
   `text-size` population has 395 one-use narrow values consumed as the
   immediately following `MIR_STORE_INDIRECT.src2`, across 155 functions.
2. T207: add selector-scoped physical-stack forwarding for narrow non-bitfield
   indirect-store values. Push the value at its definition, materialize the
   stored-to address in HL, pop the value into DE, and emit the byte/word
   store. Wide and constant-absolute stores retain their existing paths.
3. T208: retain the optimization only in the existing fallback-only fresh
   spilled retry. The first rollout adds three functions, but the function
   with two handoffs regresses both peep and nopeep execution. A structural
   cost gate therefore limits rollout to one handoff per candidate.

The ordinary census is **741/2022 (36.65%)**, +2 names and zero removals.
The stack-check census is **756/2124 (35.59%)**, also +2 and zero removals.
Both add `tbfinit.check` and `tnarrow.narwchain`; focused full peep/nopeep
validation passes with zero regressions and two checked improvements.

## Batch 29

1. T209: audit the 260 one-use branch-condition slots. Of 258 adjacent
   conditions, 256 are narrow; the shared immediate-forwarding whitelist
   omitted `MIR_BRANCH_FALSE`.
2. T210: add branch-condition forwarding only when the false edge needs no
   phi copies. A global prototype changes 130 apps and displaces two existing
   MIR selections, so production keeps it in the fallback-only fresh retry.
3. T211: profile four newly admitted candidates. The three two-block helpers
   improve or preserve both modes; the eleven-block candidate regresses peep
   execution despite a static instruction win. The mandatory gate also catches
   a 490-instruction two-block retry that regresses nopeep execution. Structural
   block-count, handoff-count, and 100-instruction caps retain the measured
   small-helper class.

The ordinary census is **744/2022 (36.80%)**, +3 names and zero removals.
The stack-check census is **759/2124 (35.73%)**, also +3 and zero removals.
Both add `a1.usage`, `adaint.acc_word`, and `bint.die`; focused full
peep/nopeep validation passes with zero regressions and nine checked
improvements.

## Batch 30

1. T212: audit 394 one-use indirect-store address slots across 132 functions.
   Of these, 204 are produced two instructions before their store; 104 narrow
   stores also have a one-use value produced in between.
2. T213: extend the planned-stack contract for that exact address/value/store
   sequence. The address is pushed first, the value is pushed above it, and
   the store pops value into DE then address into HL. A dedicated fresh retry
   keeps earlier adjacent-forward selections intact.
3. T214: profile the first three additions. Single-handoff list helpers
   regress peep execution despite static wins; the two-handoff candidate
   improves both modes. A structural minimum of two address handoffs retains
   only the measured amortized class.

The ordinary census is **745/2022 (36.84%)**, +1 name and zero removals.
The stack-check census is **760/2124 (35.78%)**, also +1 and zero removals.
Both add `tunused.main`; focused full peep/nopeep validation passes with zero
regressions and four checked improvements across the affected apps.

## Batch 31

1. T215: audit wide one-use backend slots. Seventy-five adjacent two-unit
   values are consumed as the left operand of a wide binary; all are produced
   by `MIR_UNARY`.
2. T216: extend the shared DE:HL forwarding predicate and backend-slot planner
   to this exact consumer. The binary emitter consumes its left operand first
   and immediately pushes DE:HL before loading the right operand, so the
   producer can remain resident without a four-byte slot.
3. T217: keep the feature selector-scoped in a later fallback-only retry,
   preserving every incumbent and earlier retry winner. The seven newly
   admitted functions improve both affected apps in peep and nopeep modes.

The ordinary census is **752/2022 (37.19%)**, +7 names and zero removals.
The stack-check census is **767/2124 (36.11%)**, also +7 and zero removals.
Both add `tctxops.ca_callarg`, `tctxops.ca_ret`, `tlongopt.cb_ge`,
`tlongopt.cb_lt`, `tlongopt.cc_eq`, `tlongopt.cc_gt`, and
`tlongopt.cc_lt`; focused full peep/nopeep validation passes with zero
regressions and four checked cycle improvements.

## Batch 32

1. T218: refresh the actual-emission unused-slot audit. The 534 current
   `text-size` fallbacks contain 164 unused assigned slots across 59
   functions; all are adjacent, one-use wide values. Extend the diagnostic
   with definition/consumer immediates and types so the remaining classes can
   be separated without MIR-dump guesswork.
2. T219: reject the two strongest wide-slot hypotheses. Selector-scoped float
   constant negation covers 75 records and direct float indirect-load returns
   cover nine, but neither admits a function. Also reject the numerically
   closest text-proxy fallback after forced emission miscompiles
   `attnc11.transposed_multiply_8x16`.
3. T220: rematerialize a one-use 16-bit call argument whose exact chain is a
   global/extern pointer load, zero or more constant member offsets, and an
   indirect word load. Skip the complete chain and its slots at definition,
   then reproduce it at the reverse-ABI argument push. Require exactly one use
   at every chain link and keep the feature in a later fresh fallback retry.

The ordinary census is **754/2022 (37.29%)**, +2 names and zero removals.
The stack-check census is **769/2124 (36.21%)**, also +2 and zero removals.
Both add `adaint.return_stmt` and `fint.die`; focused full peep/nopeep
validation passes with zero regressions and six checked improvements.

## Batch 33

1. T221: add a cache-use diagnostic and audit the actual BC/alternate-register
   call-argument handoffs. The current `text-size` population executes 688
   caches across 118 functions; direct narrow named loads are the strongest
   low-risk rematerializable class.
2. T222: factor the one-use call-argument scan shared by local and global
   loads. Rematerialize a one-use two-byte global/extern scalar or pointer at
   its argument push, skipping both its definition-site load and backend slot.
   Preserve the Link-80 extern+offset restriction and use the established
   symbol/extrn helpers.
3. T223: measure and remove four-byte global rematerialization after it changes
   one fallback metric but admits no function. Keep only the measured word
   path in a later fresh retry so Batch 32 winners remain unchanged.

The ordinary census is **756/2022 (37.39%)**, +2 names and zero removals.
The stack-check census is **771/2124 (36.30%)**, also +2 and zero removals.
Both add `pint.die` and `tcaslv.check_global_compound_param`; focused full
peep/nopeep validation passes with zero regressions and six checked
improvements.

## Batch 34

1. T224: enrich the call-cache audit with argument position and later-constant
   counts. Reject narrow early-stack handoff and generic prepacking: the former
   has only five sites, while the latter reproduces the current cache's exact
   instruction sequence.
2. T225: retain a cacheable wide highest-index argument directly on the
   physical argument stack from its definition through its generic call.
3. T226: push a cached narrow generic-call argument directly from BC, while
   preserving the established BC-to-HL path for specialized fastcalls.
4. T227-T228: profile remaining fallback opcodes and audit promoted local
   storage. Reuse fully promoted, non-address-taken two- and four-byte local
   holes for explicit backend-slot offsets, with a fallback-only retry and
   unchanged logical slot identities.
5. T229: reject global reverse-order argument lowering after it removes 463
   incumbent selections. MIR construction remains source-ordered; any future
   deferral must be selector-scoped.
6. T230: profile homed-selector rejection causes and reject broad
   `load-comparison-cfg` and spill-limit relaxation. The former regresses the
   peep path in both newly selected apps despite raw instruction wins; the
   latter changes no selection.
7. T231-T232: complete the homed generic-call ABI for four-byte scalar
   arguments and results. Push pair homes directly, avoiding a destructive
   BC:IY-to-DE:HL conversion, and store DE:HL results through the shared pair
   home helper.
8. T233: support homed integer/long-to-float and float-to-integer/long casts,
   preserving live BC around conversion helpers. Correct shared float-to-bool
   normalization so `-0.0f` remains false.
9. T234: emit homed long/float comparisons through the shared wide operation
   helper. Reject wide-phi copies and broad wide-CFG arithmetic after each
   changes no selection.

The ordinary census is **764/2023 (37.77%)**, eight selections above the
published baseline with no removals. The stack-check census is **781/2125
(36.75%)**, ten selections above the published baseline with no removals.
The denominator grows by one because selecting `tctxflt.cond_arr_ptr`
materializes its formerly inline-only helper `use_fptr`, which is also selected
by MIR. Focused full peep/nopeep validation passes with no regressions.

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

## Batch 21

1. T184: canonicalize exact-type byte, long, unsigned-long, and float
   conversion nodes before liveness and allocation. This extends the existing
   word representation-identity repair without conflating signed and unsigned
   wide types: exact 1/2/4-byte identities alias directly to their source,
   while the established representation-compatible 16-bit rule remains
   unchanged.

The candidate ordinary census is **711/2022 (35.16%)**, +7 names and zero
removals. The candidate stack-check census is **720/2124 (33.90%)**, also +7
names and zero removals. Ordinary additions are `tfloat4.test_basic`,
`tfloat4.test_long_float_mix`, `tfmadd.global_case`, `tpfio.main`,
`tpostptr.check_i32`, `tpostptr.check_u32`, and `tret.main`; stack-check
replaces `tfmadd.global_case` with `tunary.shui32`.

The 16 affected apps pass focused full-mode validation with zero regressions
and 56 checked cycle/size improvements. The refreshed ordinary rejection
population is led by 688 `text-size`, 138 `unary-not-cost`, 117
`dynamic-index-base-cost`, 65 `wide-constant-cost`, 62 `instruction-count`,
49 `absolute-index-cost`, 47 `absolute-address-cost`, and 46
`inline-substitution` functions.

## Batch 22

1. T185: replace singleton-only stack forwarding with a conservative planned
   handoff for nonadjacent, one-use byte/word values consumed as a later
   `MIR_BINARY.src1`. Slot planning records exact producer/consumer pairs;
   emission pushes at the definition and consumes at that exact instruction.
   Intervals must be same-block, stack-neutral, nonoverlapping, and exclude
   VLA, phi, aggregate-call, odd-argument call-result, control-flow, ABI, and
   wide-value hazards.
2. T186: keep the optimization transactional with emit/consume balance checks
   and a measured call-heavy profitability gate. Exact-upstream A/B rejected
   `tstr2.test_memchr`: seven fewer MIR instructions still regressed peep
   cycles by 50. Functions with at least eight calls therefore require an
   eight-instruction saving. The broader wide-value extension was also
   removed after miscompiling `tfloat4` and `tfmadd`.

The candidate ordinary census is **719/2022 (35.56%)**, +8 names and zero
removals. The candidate stack-check census is **728/2124 (34.27%)**, also +8
names and zero removals. Both add `adaint.xstrdup2`, `bint.xstrdup`,
`cint.xstrdup2`, `cobint.xstrdup2`, `fint.patch`, `fint.xstrdup2`,
`forint.xstrdup2`, and `pint.xstrdup`.

The 13-app focused full-mode run passes with zero regressions and 29 checked
cycle/size improvements. The refreshed ordinary rejection population is led
by 671 `text-size`, 131 `unary-not-cost`, 117 `dynamic-index-base-cost`, 65
`wide-constant-cost`, 61 `instruction-count`, 48 each
`absolute-address-cost`, `absolute-index-cost`, and `inline-substitution`, and
13 `planned-stack-cost` functions.

## Batch 23

1. T187: extend the planned narrow handoff to nonadjacent, one-use,
   fixed-stride `MIR_INDEX_ADDRESS.src1` bases. Planned state is independent
   of legacy singleton forwarding, and one shared exact-consume helper handles
   constant-index `pop hl` and dynamic-index `pop de`/`add hl,de` paths.
   Runtime strides and address results eliminated by absolute-access folding
   remain excluded.
2. T188: add a measured index-plan cost gate after exact-upstream A/B rejects
   `tfarrsub.set_intvec` at a one-instruction saving and `cobint.emit` at a
   20-instruction saving across three calls. Call-free candidates must save
   two instructions; call-containing candidates must save seven per call.

The candidate ordinary census is **723/2022 (35.76%)**, +4 names and zero
removals. The candidate stack-check census is **732/2124 (34.46%)**, also +4
names and zero removals. Both add `cint.alloc_local`, `cobint.emit_tok`,
`fint.peek`, and `tpeepal.interior_escape_store`.

The five-app exact-upstream focused full-mode run passes with zero regressions
and nine checked cycle/size improvements. The refreshed ordinary rejection
population is led by 627 `text-size`, 130 `unary-not-cost`, 117
`dynamic-index-base-cost`, 65 `wide-constant-cost`, 61 `instruction-count`,
48 `absolute-index-cost`, 47 `inline-substitution`, 45
`absolute-address-cost`, 44 `planned-index-base-cost`, and 15
`planned-stack-cost` functions.

## Batch 24

1. T189: audit the refreshed fallback population and identify eager homed
   parameter binding as the largest reusable allocator defect. Stable one-use
   narrow parameters occur in 301 fallback functions across 84 apps; loading
   every parameter at entry artificially extends all lifetimes, consumes
   colors/spills, and can force IY framing.
2. T190: add a selector-scoped lazy-parameter allocation probe. Eligible
   values are real-object, one-use, one-/two-byte nonpointer parameters with
   no reassignment and no VLA. The probe excludes them from interference and
   allocation, emits their sole use directly from the stable IX ABI offset,
   reuses exact signed/unsigned/`_Bool` normalization, forces IX framing, and
   restores every color, spill, and probe flag after the fresh-stream trial.
3. T191: retry only durable, non-speculative functions whose incumbent
   candidate already failed `text-size` or `instruction-count`, then
   re-evaluate the lazy candidate through the unchanged acceptance chain.
   Running the retry inside speculative inline-codegen attempts was explicitly
   rejected: it materialized otherwise-elided static bodies and changed the
   corpus denominator. Excluding speculative sinks restores the exact baseline
   population and prevents inline-selection state leakage.
4. T192: retain measured profitability margins for the three structural
   classes rejected by exact-upstream A/B: more than four lazy parameters,
   byte-parameter expressions, and small phi CFGs. This rejects
   `tarray6.v6`, `tkandr.uchar_mix`, and `tctxflt.cond_cmparm` without
   function-name exceptions; forced fallback removes each measured peep/nopeep
   regression.

The candidate ordinary census is **729/2022 (36.05%)**, +6 names and zero
removals. Additions are `cobint.check_idx`, `forint.resolve_idx`,
`tbool.bool_param_sum`, `tlongopt.co_sub`, `tmirslot.immediate_use`, and
`wumpus.hwum`.

The candidate stack-check census is **743/2124 (34.98%)**, +11 names and zero
removals. It additionally adds `pint.load_op_e`, `pint.loada_op_e`,
`pint.store_op_e`, `pint.storea_op_e`, and `trw.fill_buf`. The nine-app
stack-check focused full-mode run passes with zero regressions and 18 checked
cycle/size improvements.

The refreshed ordinary rejection population is led by 616 `text-size`, 130
`unary-not-cost`, 117 `dynamic-index-base-cost`, 65 `wide-constant-cost`, 56
`instruction-count`, 48 `absolute-index-cost`, 47 `inline-substitution`, 45
`absolute-address-cost`, 44 `planned-index-base-cost`, 37
`dead-local-suffix-cost`, and 8 `lazy-parameter-cost` functions.

## Batch 25

1. T193: audit stable named frame homes after lazy parameter allocation.
   A global prototype that directly reloaded stable pointer locals added six
   functions but removed nine existing MIR selections, so production keeps
   the optimization in a fallback-only fresh-stream retry.
2. T194: generalize the shared direct-home predicate from parameters to
   pointer locals only when the value is defined by `MIR_LOAD`, its local is
   not address-taken or volatile, no later store can change the slot, the
   function has no VLA, and the CFG has no backedge. The retry is excluded
   from speculative codegen and restores its selector-scoped state before
   every subsequent rescue or final decision.
3. T195: retain the measured four-instruction margin for multi-block
   candidates and reject call-heavy single-block candidates. Exact-upstream
   A/B admits `tallocx.t_grow_top` and `tallocx.t_shrink_inplace`; it rejects
   `tstr2.test_memchr`, whose ten-instruction raw saving still regresses peep
   execution by 50 cycles.
4. T196: complete the correctness review. Named volatile loads now retain
   their volatile memory flag, stable pointer-local rematerialization rejects
   them, and profitability attribution distinguishes an actually removed
   backend slot from a direct-home path that was already slotless.

The candidate ordinary census is **734/2022 (36.30%)**, +5 names and zero
removals. The candidate stack-check census is **748/2124 (35.22%)**, also +5
names and zero removals. Both add `a1.load_input_file`, `tallocx.t_calloc`,
`tallocx.t_grow_top`, `tallocx.t_shrink_inplace`, and `tallocx.t_trim`.

The final focused full-mode run for `a1`, `tallocx`, and `tvolopt` passes with
zero regressions. The refreshed ordinary rejection population is led by 653
`text-size`, 131 `unary-not-cost`, 118 `dynamic-index-base-cost`, 72
`wide-constant-cost`, 48 `absolute-index-cost`, 47 `inline-substitution`, 46
`absolute-address-cost`, 44 `planned-index-base-cost`, 37
`dead-local-suffix-cost`, 24 `dead-store-forwarding-cost`, 11
`planned-stack-cost`, 8 `lazy-parameter-cost`, and 2
`stable-pointer-local-cost` functions.

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
3. **Next structural priorities by impact:** the post-T196 ordinary census is
   led by 653 `text-size`, 131 `unary-not-cost`, 118
   `dynamic-index-base-cost`, 72 `wide-constant-cost`, 48
   `absolute-index-cost`, 47 `inline-substitution`, 46
   `absolute-address-cost`, 44 `planned-index-base-cost`, and 37
   `dead-local-suffix-cost` fallbacks. Re-bucket
   `text-size` by repeated emitted pattern before choosing the next shared
   improvement. The cost populations are deliberately measured fallback, not
   gate-removal headroom.
4. Keep same-block CSE deferred unless a new liveness model removes Item T70's
   measured slot/move regression.
5. Re-run ordinary and stack-check censuses after each structural batch and
   sweep the newly exposed near-miss layer.
6. Repeat evidence-backed structural batches until ordinary coverage reaches
   at least **50%**, preserving zero correctness and peep/nopeep regressions.
