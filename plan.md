# dcc MIR migration - current handoff

`mir-text-size-plan.md` is the authoritative migration log. Historical plans
were removed after their completed findings were folded into that log; git
history preserves them.

## Current state

- Branch: `perf/unified-regalloc`
- Published baseline: `70f66ed` (Item T112)
- Published ordinary coverage: **559/2022 functions (27.65%)**
- Current ordinary coverage: **570/2022 functions (28.19%)**
- Current stack-check coverage: **576/2124 functions (27.12%)**
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
   constants, and wide homed-vs-spilled arbitration are complete. The next
   wide slice is values crossing helper calls, which requires explicit
   clobber-aware splitting or spill/reload rather than wider coloring.
2. **Phase 4 - complete:** the conservative per-reference pointer classifier is
   active with zero removals.
3. **Next structural priorities by impact:** remeasure the post-T122 rejection
   population, then prioritize helper-call wide splits and the largest
   zero-spill selector/profitability classes. Float/non-scalar returns,
   repeated general comparisons, and dynamic indexes remain known classes.
   Do not retry stride-1 dynamic indexing without a smaller address-add
   contract; the first correctly arbitrated version retained zero changes.
4. Keep same-block CSE deferred unless a new liveness model removes Item T70's
   measured slot/move regression.
5. Re-run ordinary and stack-check censuses after each structural batch and
   sweep the newly exposed near-miss layer.
6. Repeat evidence-backed structural batches until ordinary coverage reaches
   at least **50%**, preserving zero correctness and peep/nopeep regressions.
