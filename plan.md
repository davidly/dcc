# dcc MIR migration - current handoff

`mir-text-size-plan.md` is the authoritative migration log. Historical plans
were removed after their completed findings were folded into that log; git
history preserves them.

## Current state

- Branch: `perf/unified-regalloc`
- Published baseline: `9b4a7c5` (Items T133-T136)
- Published ordinary coverage: **583/2021 functions (28.85%)**
- Batch 8 ordinary coverage: **593/2021 functions (29.34%)**
- Batch 8 stack-check coverage: **598/2123 functions (28.17%)**
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

- `cfg-backedge`: separate correctness project; three forced candidates are
  confirmed miscompilations.
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
3. **Next structural priorities by impact:** the post-T140 census is led by
   986 `text-size`, 81 `selector`, 78 `instruction-count`, 73
   `absolute-address-cost`, and 56 `absolute-index-cost` fallbacks. Profile
   the largest zero-spill homed candidates that already emit but lose to
   spilled output, then attack one repeated instruction pattern rather than
   widening a final cost gate. Float/non-scalar returns remain a known class.
   Do not remove the repeated-load/comparison gate until speculative selector
   state is fully transactional.
4. Keep same-block CSE deferred unless a new liveness model removes Item T70's
   measured slot/move regression.
5. Re-run ordinary and stack-check censuses after each structural batch and
   sweep the newly exposed near-miss layer.
6. Repeat evidence-backed structural batches until ordinary coverage reaches
   at least **50%**, preserving zero correctness and peep/nopeep regressions.
