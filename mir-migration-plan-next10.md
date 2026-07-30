# MIR migration: next ~10% coverage plan

Continuation of `mir-migration-plan-100.md` (fully complete, Items 1-100,
174/2378 functions / 7.32% coverage, 0 regressions, 18 apps improved / 0
regressed). Per that plan's own Item 100 handoff and `SKILL.md`'s discipline,
this is a fresh document with its own item numbering rather than a
continuation of the old numbering - re-derived from the current census, not
assumed stale priorities.

## Goal

Grow MIR coverage from **7.32% (174/2378)** toward roughly **~17%** (an
additional ~240 functions), by attacking the single dominant blocker
identified at Plan-100's close: the systemic `text-size` fallback population
(2146 of 2378 functions, 90.2% of all fallback), all attempted through
`mir_try_emit_spilled_scalar_cfg`, uniformly ~2x more expensive than legacy
per Plan-100's Item 94 bucketing - not a near-miss population, a systemic
emitter-quality bug.

## Hard criteria (non-negotiable, per explicit instruction)

1. **Zero performance regressions.** Every commit's full-mode diff must show
   no app regressed vs. `tests/perf_baselines.csv`. A genuinely-improved
   metric may be accepted into the baseline only after a clean full run
   proves it intentional (existing `-UpdatePerfBaseline` policy, unchanged).
2. **Full `-Mode full -Extended` run before every commit.** No commit lands
   on a partial/fast-only validation. This is slower than the old Plan-100
   cadence at times, which is exactly why the process-speed section below
   matters.

## Root cause (recap, see SKILL.md for full detail)

`mir_emit_scalar_compare` (`dcc_mir.c`, near the scalar-compare lowering)
unconditionally materializes an explicit 0/1 boolean for every comparison -
even when the comparison's only consumer is an immediately following
conditional branch. That boolean gets spilled to a backend slot and reloaded
just to be re-tested by `MIR_BRANCH_FALSE`. `dccpeep`'s same-basic-block
redundant-reload pass removes one of the two resulting store/reload
round-trips, but the second is a genuinely dead store the existing peephole
passes don't target. The narrow `mir_try_emit_comparison_branch` selector
already fuses compare+branch, but only for one whole-function shape
(`if (a OP b) return X; return Y;`); it never fires for the general in-loop
or mid-function case that `mir_try_emit_spilled_scalar_cfg` handles for 91%
of the corpus.

## Staged items

Each item follows the SKILL.md fast-migration loop (census before/after,
smallest reusable edit, full-mode validation on affected apps, milestone
validation at phase boundaries). Numbering restarts at 1 for this plan.

**Phase A - measurement and hypothesis narrowing (Items 1-2)**

1. Fresh census with per-function byte/instruction gap re-bucketed
   specifically for the `text-size` population (repeat Plan-100 Item 94's
   analysis on today's corpus - the population may have shifted composition
   even though the fallback count stayed constant across Phase 9/10, which
   made no `dcc_mir.c` changes touching this path).
2. Pick 3-5 representative `text-size` functions spanning: (a) a single
   `if (cmp) branch` inside a larger function, (b) a `while (cmp) { ... }`
   loop condition, (c) a compound condition (`&&`/`||` of two compares).
   Forced-accept diff each (`DCC_MIR_FORCE_ACCEPT_FUNCTION`) against legacy
   to confirm the double-materialization pattern generalizes identically in
   all three shapes before committing to a general fix.

**Phase B - dead-store elimination (Items 3-5, lowest risk, likely fastest
yield)**

3. Fix the specific dead-store case: when a materialized boolean's *only*
   use is a single immediately-following branch test in the same block,
   skip materialization/spill entirely and lower directly to the branch's
   comparison, following `mir_try_emit_comparison_branch`'s existing
   fusion but from within `mir_try_emit_spilled_scalar_cfg`'s general path
   instead of only its narrow whole-function selector.
4. Extend the fusion to the loop-condition shape (comparison feeding a
   `while`/`for` condition test), reusing the same fused lowering primitive
   from Item 3 rather than a second implementation (Item 86 discipline: one
   formula, not two).
5. Re-run the full census; expect a meaningful slice of the 2146 `text-size`
   population to either newly clear the cost gate (promoted to `mir`) or at
   least narrow enough to reclassify by root cause for Phase C.

**Phase C - compound conditions (Items 6-8, higher risk: CFG shape)**

6. Investigate whether `&&`/`||` short-circuit compound conditions can reuse
   the Item 3/4 fusion per sub-comparison (each sub-branch is itself a
   simple compare+branch), or whether they need a distinct selector due to
   the extra intermediate blocks. Do not widen Item 3's gate speculatively;
   profile a forced-accept A/B first (skill rule 4).
7. Implement whichever shape Item 6's investigation supports with evidence.
8. Milestone validation + census delta; document in this plan's Execution
   Log (see below) exactly like Plan-100's format.

**Phase D - close the loop (Items 9-10)**

9. Re-run Plan-100 Item 94/96's classification against the new census:
   recompute the fallback-reason table and MIR-required-mode readiness
   assessment at the new coverage level.
10. Write the closing report + next handoff (same shape as Plan-100 Item
    100), including a fresh priority-ordered blocking-class list for
    whatever plan follows this one.

Items are deliberately fewer and coarser-grained than Plan-100's, because
this plan targets one root cause with a small number of higher-yield,
higher-risk changes, rather than many small independent pattern folds (per
SKILL.md's own note that the small-fold vein ran dry near Plan-100's start).
Expect Phase B/C items to each take meaningfully longer per item than a
typical Plan-100 item; the process-speed measures below are aimed
specifically at keeping the *validation* overhead from dominating that time.

## Process speed-up measures adopted for this plan

Investigated and implemented this session, ahead of starting Item 1:

1. **`scripts/mir-migration-census.py --jobs`** (committed `1ce33a9`): census
   compiles are now parallelized (default: CPU count) via
   `ThreadPoolExecutor`, since each app compile is an independent, short subprocess.
   Full 314-app census wall time: **~23.7s -> ~5.8s** on this machine (~4x).
   Output is unchanged (byte-identical, verified against `--jobs 1`) - use
   this for every "snapshot before editing" / "compare snapshots" step in the
   fast migration loop; it is a pure speed win with no discipline change.

2. **`scripts/runall.ps1 -FailFast`** (committed `143f587`): stops
   dispatching new apps as soon as the first correctness failure or per-app
   performance regression is observed (apps already in flight finish; the
   check reuses `Test-PerfRegressions` unchanged, so it can never disagree
   with the end-of-run check). Use during **iteration-tier** validation on a
   candidate batch so a broken change is caught in seconds instead of after
   the full suite completes. **Do not use `-FailFast` for the mandatory
   pre-commit full run** - the hard criteria above require every app's
   status to be known before a commit, and a fail-fast run by definition
   leaves later apps unstarted.

3. **`-ThrottleLimit` oversubscription** (measured this session, not yet a
   permanent default - proposing, not changing, since it affects every
   invocation): on this 10-core machine, a full `-Mode full` run (no
   `-Extended`) took 91.3s at the default throttle (10), 68.9s at
   `-ThrottleLimit 20` (~25% faster), and 77.9s at `-ThrottleLimit 30`
   (slightly worse than 20 - diminishing/negative returns past ~2x core
   count). These are short, I/O- and emulator-launch-bound tasks, not
   CPU-bound, so oversubscribing the throttle pool keeps cores fed while
   individual workers block on process spawn/exit. **Recommendation**: pass
   `-ThrottleLimit <2x core count>` explicitly for milestone-tier runs on
   multi-core machines rather than changing the script's default (which
   would also affect constrained/CI-shared machines where 2x could cause
   contention) - a per-invocation flag, not a default change, avoids any
   infra-scope-creep risk.

4. **Not pursued this session**: further parallelizing `runall-extended.ps1`
   relative to the main suite (currently sequential after it) - both already
   parallelize internally, and combining them into one throttle pool would
   risk conflating two independently-meaningful pass/fail reports for
   uncertain wall-time benefit; revisit only if Extended's own wall time
   becomes the bottleneck (it is currently ~19s vs. ~70-90s for the main
   suite).

Net effect: the two committed changes (parallel census, `-FailFast`) reduce
iteration-loop latency directly; the throttle observation is a recommended
per-invocation flag for milestone/final runs. None of this relaxes the two
hard criteria above - every commit in this plan still requires a full,
non-fail-fast, non-oversubscribed-if-you-want-maximum-caution `-Mode full
-Extended` pass with zero regressions.

## Execution Log

(Entries added as items complete, in the same style as
`mir-migration-plan-100.md`'s Execution Log: item number, what was done,
evidence, commit hash.)

- **Process tooling (pre-Item-1)**: implemented and validated the three
  speed-up measures above. Commits `143f587` (`-FailFast`), `1ce33a9`
  (parallel census). Both verified with a full `-Mode full -Extended` run
  (314/314 + 196/196, 0 regressions) before pushing.

- **Items 1-2 (investigation only, no code change)**: fresh full census
  confirmed coverage unchanged at 174/2378 (7.32%) since this plan's
  baseline; re-bucketing the `text-size` fallback gap reconfirmed
  Plan-100's finding that the population is uniformly ~2x over cost (98%
  byte-gap >64, 95.6% instruction-gap >4), not near-miss. Forced-accept
  diffs of representative shapes (`tests/tret.c` trivial returns,
  `tests/tmirfuse.c`'s `nseq`/`nsne`/`nult`/`nuge` whole-function compare
  shape) found that SKILL.md's "Known root cause" section describing an
  unfixed `mir_emit_scalar_compare` double-materialization bug is **stale**:
  code comments citing Plan-100 Items 1/4/25/27 show
  `mir_binary_is_fusable_comparison` + `mir_emit_fused_comparison_branch`
  already fixed this during Plan-100, and direct inspection of `nseq`
  confirmed it works correctly. The actual waste found in `nseq`'s output
  was a genuinely new bug: a duplicate/dead trailing epilogue (see Item 3).
  This plan's original Items 3-4 (targeting the already-fixed compare bug)
  are superseded; Item 3 below documents the pivot. SKILL.md's stale
  section should be corrected in a follow-up documentation commit.

- **Item 3: dead trailing-epilogue deduplication in
  `mir_try_emit_spilled_scalar_cfg`**. `mir_try_emit_spilled_scalar_cfg`
  (the largest selector, 2253 functions) unconditionally called
  `mir_emit_virtual_iy_epilogue(out)` again after its main instruction
  loop, even when the function's last IR instruction was `MIR_RETURN`
  (whose own case already emits the epilogue) - producing dead,
  unreachable `ld sp,ix / pop ix / ret` after every return-terminated
  function through this selector. Fixed by skipping the trailing call when
  `mir.insns[mir.count - 1].opcode == MIR_RETURN`.
  A naive version of this fix (skip only, no gate compensation) raised
  coverage 174 -> 180 with a clean census delta, but the SKILL.md-mandated
  focused `-Mode full` run exposed 3 real regressions
  (`tmirslot.dead_store_elision`, and 2 of 3
  `tvla.vla_sizeof_op_{add,mullhs,sub}` combined) - the byte savings from
  deduplication tipped these already-marginal functions over the
  acceptance gate, exposing a pre-existing, unrelated MIR
  parameter-homing inefficiency (a single-use parameter is homed to a
  local stack slot and reloaded instead of reused directly from its
  incoming `ix` offset) that the gate's static text-size proxy could not
  see as a real regression. Rather than fix that separate inefficiency or
  add a function-name exception (forbidden by skill rule 6), the fix was
  changed to decouple emission from the gate: a new
  `mir_spilled_scalar_cfg_elided_epilogue_bytes` global records the
  elided epilogue's text length (measured via `fmemopen`, since
  `mir_emit_virtual_iy_epilogue` only reads module globals and has no
  side effect beyond writing text) whenever the dedup applies, and the
  acceptance-gate's `generated_size` computation adds this back for the
  `spilled-scalar-cfg` selector before the accept/reject comparison -
  restoring the exact pre-fix gate outcome while leaving the real emitted
  text deduplicated for every function already accepted independently.
  Verified: full census with `--compare` against the pre-fix baseline
  shows exactly 0 newly-emitted and 0 no-longer-emitted functions
  (174/2378 unchanged); the focused validation
  (`tbool,tc89size,tstrconv,tvla -Mode full`) now shows 0 regressions and
  4 improvements (`tstrconv` -0.1% cycles/-1.06% bytes peep,
  `tvla` -0.43% bytes peep); full `-Mode full -Extended` passed 314/314 +
  196/196. This is a pure dead-code-removal win with no behavioral or
  gate-decision change. Commit `<pending>`.
