# dcc MIR migration: accelerated roadmap to 60%

`mir-text-size-plan.md` is the authoritative experiment log. This file is the
current execution plan and handoff.

## Goal and current state

The immediate target is **1,216/2,026 ordinary functions (60.02%)** in
production MIR emission, without ordinary or stack-check removals and without
peep/nopeep regressions. Mixed-mode transactional fallback remains in place.
The long-term goal is 100% MIR-required coverage and removal of legacy
capture/replay only after the runnable and extended corpora pass.

- Branch: `perf/unified-regalloc`
- Published baseline: `45cf3f0`
- Published ordinary coverage: **890/2026 (43.93%)**
- Published stack-check coverage: **912/2128 (42.86%)**
- Current ordinary coverage: **895/2026 (44.18%)**
- Current stack-check coverage: **917/2128 (43.09%)**
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

The branch is CI-green, currently at **895/2026 ordinary (44.18%)**, still
**+17 short of even the first 45% milestone** and +321 short of the 60%
target. T389-T392 (this segment) exhaustively re-ranked and forced-accept
A/B-tested every remaining fallback bucket's near-miss population (12+
buckets, 150+ candidates) and found the per-bucket static-metric mining
technique that produced T383-T388's gains is now **exhausted**: only two
small wins remained findable (T388 +4, T391 +1), 16 more confirmed
correctness bugs were found (harmless, already gated off), and the
remaining real winners in every other bucket (most notably
`planned-index-base-cost`'s 13-function cluster) have **no safe
generalizable predicate** - structurally identical candidates land on both
sides of win/loss, so promoting any of them would require a prohibited
name-based exception or accepting a regression.

**Continued per-bucket near-miss mining is no longer a viable path to 45%,
let alone 60%.** The two next-most-promising leads are both architecture
items, not gate nudges:

1. **`campaign2-call-effect-analysis`** (was `absolute-address-cost`'s lead):
   T390 found the real blocker is a call-side-effect/aliasing gap, not a CSE
   gap - a global struct member's `loadind` cannot be safely reused across
   an intervening opaque call without proving the callee doesn't write back
   to it. Needs either a runtime-function side-effect whitelist (auditable
   against `DCCRTL.MAC`) or real interprocedural alias analysis.
2. **`campaign-phi-fallthrough-architecture`** (was `mir60-boolean-control`'s
   phi-fallthrough lead, T384): the `phi-fallthrough-cost` bucket (44
   functions) needs real phi-forwarding-across-labels support, confirmed
   in T391 as the actual blocker for `tinline.edge_and`/`edge_conditional`
   independent of any other bucket's fix.
3. **`next50-slot-intervals`** (Campaign 2 item 3, use-position backend-slot
   intervals) remains the highest-expected-yield item on paper since it is
   structurally upstream of `block-cse-cost`, `wide-store-cost`, and
   `planned-index-base-cost` simultaneously - but it is also the largest,
   highest-risk undertaking (replaces whole-value backend-slot lifetimes
   with real interval tracking). Build behind a feature control, validate
   on a train/holdout app split, and stop/re-rank if a bounded effort does
   not clear a double-digit net gain, per the item's own scoped criteria.

Any of these three is a multi-session engineering project requiring careful
design before the first line of code, not a same-session gate tweak. Follow
the commit cadence below for every change: focused cohort during
development, one fresh full-extended gate immediately before commit, push,
and wait for CI green before starting the next item.

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
