# MIR Emitter Migration Plan — Next 100 Items (fresh, 2026-07-30)

This plan **replaces** the prior `mir-emitter-migration-plan.md` and
`mir-migration-plan-100.md` (both retired/deleted from the working tree;
history preserved in git at commit `c1dafe1` and earlier). It was built by
re-measuring the corpus from scratch and reading the current
`src/dcc/dcc_mir.c` rather than continuing stale item numbering. Read
`.github/skills/mir-migration/SKILL.md` first — this document is the
roadmap; the skill is the operating discipline (fast migration loop,
diagnostics, validation tiers, baseline policy). Nothing here overrides the
skill's non-negotiable rules.

## Why a fresh plan instead of continuing the old numbering

The retired plans enumerated small, mostly-independent pattern folds
(constant-divisor materialization, assignment-alias folding, divmod
fusion, dead promoted-local stores, etc.). Grepping the branch history
shows that lineage is already substantially executed:

```
2eb36eb MIR: materialize constant divisors directly without a slot (Item 16)
9c556ec MIR: fold constant scalar comparison operations before slot assignment (Item 14)
7b20e39 MIR: fold constant scalar binary operations before slot assignment (Item 13)
de0174f MIR: eliminate dead promoted-local stores via per-object backward liveness (Items 11/12)
164ae0e MIR: forward binary/unary/divmod results to a following store (Items 6/7/8)
403f0d7 MIR: reuse dead wide-compare operand slot for narrow boolean (Item 5)
fed34c9 MIR: general constant-multiply strength reduction (Item 37)
ad3acea Plan-100: Item 2 verified already satisfied (infrastructure in place, no code change)
c3449a9 Plan-100: Item 1 verified already satisfied (no code change)
```

Two different documents (a "next 20" plan and a "100-item" plan) reused
overlapping item numbers, and today's session was already finding items
"verified already satisfied" — a sign that vein of small folds is running
dry. Continuing to number against either stale document would be
confusing and would keep prioritizing low-yield items. This plan starts
from a fresh full-corpus census and direct assembly inspection instead.

## Current measured state (2026-07-30, branch `perf/unified-regalloc`)

```
$ sh src/dcc/build-dcc.sh
$ python3 scripts/mir-migration-census.py --output build/fresh-census.tsv

MIR outcomes
   2109  fallback text-size
    165  mir      accepted
     22  fallback inline-substitution
     17  fallback instruction-count
      2  fallback cfg-backedge
      2  fallback cfg-block-count
      1  fallback pointer-array
      1  fallback selector

Final selectors
   2207  spilled-scalar-cfg
    112  homed-scalar-cfg

Coverage: 165/2319 functions (7.12%)
```

**Every one of the 2,109 `text-size` fallbacks is attempted through the
same selector, `mir_try_emit_spilled_scalar_cfg`.** That concentrates all
the evidence on one function's codegen strategy rather than 2,109
independent problems.

### The gap is not "near miss" — it is systematic

Bucketing the byte/instruction gap (`generated - captured`) for the 2,109
`text-size` fallbacks:

| Byte gap (generated − captured) | Count |
| --- | ---: |
| 17–32 | 5 |
| 33–64 | 14 |
| **> 64** | **2,090 (99.1%)** |

| Instruction gap | Count |
| --- | ---: |
| ≤ 0 (already ≤ legacy) | 20 |
| 1–2 | 42 |
| 3–4 | 6 |
| **> 4** | **2,041 (96.8%)** |

Only 3 functions in the entire corpus are single-block "near cost"
misses. The prior plans' priority-2 heuristic ("near-cost real
functions") barely applies any more — this population is **uniformly ~2×
more expensive**, not marginally short.

### Root cause, confirmed by direct assembly inspection

Two representative fallbacks were force-accepted
(`DCC_MIR_FORCE_ACCEPT_FUNCTION=<fn>`) and diffed against the legacy
output:

**`check_s` (`tests/tesc.c`, byte-identical copy also in `tstr3.c` /
`tsyntax.c` — one fix, three apps)**: source is
`if (strcmp(got, expected) != 0) fail_s(...)`. Legacy backend:

```
call __scmp
pop bc
pop bc
ld a,h          ; test the raw result directly
or l
jp z, L108      ; branch straight off it
```

MIR (`spilled-scalar-cfg`, pre-peephole):

```
call __scmp
pop bc
pop bc
ld (ix-2),l     ; spill the comparison operand for no reason
ld (ix-1),h
ld l,(ix-2)     ; immediately reload it
ld h,(ix-1)
push hl
ld hl,0
ex de,hl        ; a full 16-bit compare-with-zero dance
pop hl
or a
sbc hl,de
ld hl,0
jp nz,L114      ; materialize an explicit 0/1 boolean
jp L115
L114: inc l
L115:
ld (ix-2),l     ; spill the boolean AGAIN
ld (ix-1),h
ld l,(ix-2)     ; reload it again just to re-test it
ld h,(ix-1)
ld a,h
or l
jp nz,L116      ; finally branch
```

`dccpeep` removes *one* of the two redundant reloads (same-basic-block
read-after-write) and folds `push/ld hl,0/ex de,hl/pop hl` into `ld
de,0`, but it cannot remove the **second** spill: it is a genuinely dead
store (never reloaded from again anywhere in the function), which is a
different bug class than the redundant-reload pass targets, and it
doesn't know the comparison's only real consumer is a branch three
instructions later.

**`and_expr` (`tests/adaint.c`, a `while` loop)**: the identical
spill/reload dance appears on the loop test value, plus a redundant
"continue" landing block (`L3575: jp L3573`) with nothing between it and
its target — a bare jump-to-jump the legacy backend never emits because
its loop lowering doesn't need a separate continuation block for the
simple case.

**Conclusion.** `mir_try_emit_spilled_scalar_cfg` unconditionally routes
every scalar comparison through `mir_emit_scalar_compare` (which *always*
materializes an explicit 0/1 value via a label/`inc l` dance — see
`dcc_mir.c` ~line 4939), stores it to a backend slot, and only later
reloads it for `MIR_BRANCH_FALSE`'s own (already-decent) direct-test
sequence. The narrow `mir_try_emit_comparison_branch` selector *already
avoids this* — it fuses compare+branch directly — but it only accepts
whole functions of the exact shape `if (param OP param) return A; return
B;`. It does not run for anything else, and 91% of the corpus is
"anything else." This is the single highest-leverage fix available, and
it is structural (touches the shared emission function, not any
app/function name), satisfying skill rule 6.

## Prioritization framework (from the skill, applied here)

Score every candidate on **yield** (functions/apps unlocked), **reuse**
(removes a repeated pattern vs. one-off), **risk** (straight-line scalar
< acyclic CFG < calls/PHIs < loops/VLAs < large CFG/inlining), **perf
confidence** (forced full-mode A/B), **test quality**, and **merge
conflict cost** (overlap with other `dcc_mir.c` work). Because the
dominant remaining class (spilled-scalar-cfg codegen quality) is
reachable by construction from *every* remaining fallback, expect Phase 1
items to move the coverage number far more than the historical
few-functions-per-item pace — validate with the **full** census and a
**full-mode** milestone run after each Phase 1/2 item, not just a couple
of named apps, because the blast radius is the whole corpus.

**Performance bar**: the user's goal is not bare non-regression — a
migrated function should ideally beat the legacy AST emitter's cycle
count, not just match it. Treat "generated ≤ captured" (the existing
admission gate) as the floor, and record every case where the MIR
version is measurably *faster*, not just smaller, as the real signal of
success (skill rule 4: smaller text is not proof of faster code — always
run the affected apps).

## The 100-item roadmap

Each item is a single reusable concept (skill guidance: 1–20 functions,
one concept per batch — here "functions" is often "all functions matching
a structural predicate," which for Phase 1–2 can be hundreds at once).
Follow the **Universal per-item playbook** below for every item. Titles
are structural, not app/function-name-specific, per skill rule 6; named
apps are starting points for investigation (Step 3), never the target
itself.

### Phase 1 — Comparison/branch fusion & boolean-materialization elision (Items 1–12)

The anchor phase. Removes the `mir_emit_scalar_compare` "always
materialize 0/1" behavior for the overwhelmingly common case where a
comparison's only consumer is a branch.

| # | Title | Discriminator / starting evidence | Notes |
|---|---|---|---|
| 1 | Fuse a single-use scalar comparison directly into its consuming `MIR_BRANCH_FALSE` inside `mir_try_emit_spilled_scalar_cfg` | `mir_value_use_count(compare->dst) == 1` and that use is a `MIR_BRANCH_FALSE` | Anchor item. Mirror `mir_try_emit_comparison_branch`'s fused sequence, generalized to any CFG shape. Everything else in Phases 1–2 builds on this. |
| 2 | Extend fusion to unsigned comparisons and the sign-bias sequence | `mir_emit_scalar_compare_biased_right` | Same fusion, unsigned/biased operand path. |
| 3 | Extend fusion when an operand is a call result, not just `MIR_PARAM`/`MIR_CONST` | `check_s` (`tests/tesc.c`/`tstr3.c`/`tsyntax.c`) | The case study function itself; confirm identical fix applies to all 3 apps at once. |
| 4 | Fuse through a `!` (logical negation) feeding a branch | invert the fused condition instead of materializing NOT then re-testing | |
| 5 | Fuse each term of a `&&`/`||` short-circuit chain individually | one `MIR_BRANCH_FALSE` per operand | Don't assume only the final term is fusable. |
| 6 | Fuse when the branch condition arrives via a `MIR_PHI` (ternary-in-condition) | | **Deferred** (see Execution Log): needs cross-block code motion the current one-pass emitter can't do; actually Phase-4-class risk, not "lower risk" as originally assessed. |
| 7 | Tighten the "single use" precondition to an exact `mir_value_use_count` check, not positional scanning | | Safety net for out-of-order MIR sequences. |
| 8 | Remove the now-dead backend slot allocation for fused comparisons in `mir_prepare_backend_slots` | | The slot must never be *counted*, not just unused at emission — shrinks `frame_bytes` and stack-check cost too. |
| 9 | Add `DCC_MIR_FUSE_REPORT=1`: per-function fused-vs-materialized comparison counts | | Diagnostic only; makes future regressions visible without re-reading assembly. |
| 10 | Add `tests/tmircmpfuse.c`: every operator × signed/unsigned × bare/negated condition, clang baseline | | Permanent regression fixture. |
| 11 | Full census re-run; validate the coverage jump with full-mode `runall.ps1` on every newly admitted app | | Expect a large coverage jump — verify it's a real perf win, not just smaller text (skill rule 4). |
| 12 | Milestone: `-Mode full -Extended`; update `perf_baselines.csv` only for genuinely improved rows; snapshot coverage to repo memory | wide safety net | |

### Phase 2 — Backend-slot live-range hygiene / dead-store elimination (Items 13–24)

Independent of comparisons specifically: no value should be spilled if
its live range never needs it.

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 13 | Skip slot allocation when a definition's single use is the very next instruction with no intervening label/call/aliasing store | | General register hand-off case. |
| 14 | Extend Item 13 across a `MIR_CALL` boundary when the value is consumed before the call or is itself an already-pushed argument | | |
| 15 | Extend Item 13 across a `MIR_LABEL` with exactly one predecessor (textually split but structurally straight-line) | done | see Execution Log |
| 16 | Eliminate the `check_s`-class dead store: a slot written but never read again anywhere in the function | evidence: second spill in the `check_s` case study, which `dccpeep`'s same-block reload pass cannot see | verified already satisfied |
| 17 | Generalize Item 16 with a per-object backward-liveness pass over the whole function | reuse the Items 11/12 (dead promoted-local stores) liveness infra as a model | verified already satisfied |
| 18 | Audit `mir_emit_spilled_phi_copies` for copies into a slot Items 13–17 already proved dead on all live incoming edges | | verified already satisfied |
| 19 | Ensure slot-count *accounting* (`mir_prepare_backend_slots`) and the real emission-time skip decision share one predicate function | repo lesson: drift between an accounting pass and the real emission path previously caused a stack-corruption bug (Item 16 divisor/dividend work, documented in perf-optimization memory) | High caution item. Audited, no code change. |
| 20 | Add `DCC_MIR_SLOT_REPORT=1`: slots requested vs. slots still read, per function | | done |
| 21 | Add `tests/tmirslot.c`: immediate-use, cross-call, cross-label, and dead-store elision cases, clang baseline | | done |
| 22 | Full census + full-mode validation of every changed app | | done |
| 23 | Dynamic cycle-count comparison (peep and nopeep) for the largest-yield apps | skill rule 4 | no candidate — see Execution Log |
| 24 | Milestone checkpoint; baseline update for genuinely improved rows only | wide safety net | done |

### Phase 3 — Constant / small-operand instruction-selection fast paths (Items 25–34)

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 25 | Emit `ld a,h / or l` directly for compare-with-constant-zero at the Phase-1 fusion site | pre-peep evidence in the `check_s` case study (`push/ld hl,0/ex de,hl/pop hl/or a/sbc hl,de`) | Do this at emission time, don't rely on `dccpeep`'s `push_lde_pop`/`cmp0_or_hl` passes to clean it up post hoc. |
| 26 | Add an 8-bit-range compare-with-small-constant fast path (`cp` byte form) instead of full 16-bit `sbc hl,de` | | |
| 27 | Add a sign-bit test (`bit 7,h`) fast path for `<`/`>=` against constant 0 | | |
| 28 | Re-check whether `%`/`/` by a power of two reaching `spilled-scalar-cfg` already gets `dccpeep`'s `pass_const_divmod_helpers` treatment post-emission | may be "verified already satisfied" — check before implementing (skill rule 1) | |
| 29 | Extend call-argument rematerialization to a constant operand consumed by a post-call comparison/binary op | current rematerialization only covers "single-use call arguments" per the (retired) progress doc | |
| 30 | Audit `mir_mul_const_fast_path_eligible` for additional profitable multiplier shapes the fresh census surfaces | | |
| 31 | Add an `inc (ix+n)`/`dec (ix+n)` in-place fast path for ±1 updates to a spilled slot | mirrors the legacy backend's already-proven idiom (perf-optimization memory) | |
| 32 | Add `tests/tmirconstfast.c` covering every new fast path, clang baseline | | |
| 33 | Full census + full-mode validation | | |
| 34 | Milestone checkpoint | | |

### Phase 4 — CFG shape hygiene (Items 35–44)

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 35 | Collapse a label immediately followed only by an unconditional jump into a direct predecessor retarget | `and_expr` case study's `L3575: jp L3573` | Do this at MIR emission time for every MIR-emitted function, not just relying on `dccpeep`'s jump threading. |
| 36 | Generalize to transitive jump-to-jump chains | bound iteration to avoid cyclic label chains | |
| 37 | Remove a `MIR_LABEL` with zero remaining predecessors after Items 35–36 | | Keeps `mir_cfg_block_count()` accurate so it doesn't wrongly trip the block-count gate. |
| 38 | Reuse an already-live value across a two-predecessor join when both predecessors define it identically, instead of spilling | build on the loop-header object-phi work already landed (commits `6144885`, `1ffeb1e`, `729bc11`) | Extend to non-loop conditional joins. |
| 39 | Extend Item 38 to a join where only one predecessor differs and the other is a plain fallthrough | | |
| 40 | Re-run the fresh census after Items 35–39 to see whether `cfg-block-count`/`cfg-backedge` fallbacks move to accepted purely from lower block counts | | Mandatory re-scoping step before Phase 5. |
| 41 | Add `DCC_MIR_PHI_REPORT=1`: phi-join reuse hits/misses | | |
| 42 | Add `tests/tmircfgshape.c`: nested if/else value joins + loop continue-block collapsing, clang baseline | | |
| 43 | Full census + full-mode validation | | |
| 44 | Milestone checkpoint | wide safety net | |

### Phase 5 — Loop competitiveness generalization (Items 45–56)

Skill risk ordering places loops/backedges above only large-CFG/inlining
— treat this phase with the most caution so far.

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 45 | Re-scope: which functions still fail on `cfg-backedge` after Phases 1–4 | mandatory fresh census, not the old count | |
| 46 | Generalize `mir_try_emit_countdown_loop` to a runtime bound already resident in a parameter/local | | |
| 47 | Generalize `mir_try_emit_accumulator_loop` to more than one independently BC/DE/IY-allocatable accumulator | | |
| 48 | Extend `mir_try_emit_unsigned_division_loop` to the signed case | if the re-scoped census shows a signed-loop fallback family | |
| 49 | Verify the specialized loop selectors don't duplicate the materialize-then-retest comparison bug fixed in Phase 1 | | |
| 50 | Extend loop-invariant hoisting (commit `729bc11`) from the homed case to the spilled case using Phase 2's slot-hygiene machinery | | |
| 51 | Re-profile the 10 VLA power-of-two loop fallbacks under the now-cheaper general loop path | do NOT remove the gate without checking exact affected functions (skill rule 1) — likely stays a small explicit exception | |
| 52 | Add a general (non-countdown, non-accumulator) loop selector for an arbitrary acyclic loop body with a backedge | highest-risk item in the plan so far | Gate behind forced A/B profiling before any production admission. |
| 53 | Dynamic profiling (`dccprof.ps1`) A/B for every function Item 52 admits | skill rule 4 — mandatory, not optional, for this item | |
| 54 | Add `tests/tmirloopgen.c` covering the new selector's shapes, clang baseline | | |
| 55 | Full census + full-mode validation | | |
| 56 | Milestone checkpoint including `-Extended` | wide safety net | |

### Phase 6 — Cost model & inline-substitution admission (Items 57–66)

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 57 | Re-measure `inline-substitution` fallbacks with the fresh census | confirm none were incidentally fixed by Phases 1–5 | |
| 58 | Build a static cost model (instruction count × call-site count vs. one-time expansion) | the previously-tried unconditional expansion enlarged nested bodies and didn't admit callers (documented in the retired progress doc) | |
| 59 | Prototype cost-gated substitution for single-call-site static-inline functions only | least risky subset first | |
| 60 | Extend to multi-call-site static-inline functions only when the callee body is provably small | reuse Phase 2's slot-count accounting as the size proxy | |
| 61 | Verify substituted bodies get the same Phase 1–4 quality treatment as top-level functions | no separate/weaker code path for substituted bodies | |
| 62 | Forced A/B profiling for every newly admitted inline-substitution candidate | skill: structural acceptance exceptions require profiling first | |
| 63 | Add `tests/tmirinline.c`: single- and multi-call-site admission | | |
| 64 | Full census + full-mode validation | | |
| 65 | Dynamic profiling for any app whose code size grows from inlining | confirm growth is still a net cycle win | |
| 66 | Milestone checkpoint | | |

### Phase 7 — Pointer-array, aggregate, and VLA structural classes (Items 67–76)

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 67 | Re-measure the `pointer-array` fallback with the fresh census | | |
| 68 | Implement stride-aware lowering/emission for a declared pointer-to-array reaching `mir_has_declared_pointer_array`'s gate | design reference: legacy `ast_index_deref_pointer_array_collect` (repo memory `dcc-ast-migration.md`), adapted to MIR address/index instructions | |
| 69 | Add a regression fixture mirroring `tests/tptrlhs.c`'s shapes through the MIR path (force-accept for validation) | | |
| 70 | Re-audit remaining VLA fallbacks beyond the intentional power-of-two gate; classify by whether Phases 1–6 change their cost profile | | |
| 71 | Extend struct/aggregate size ceiling in `mir_try_emit_spilled_scalar_cfg` only after profiling shows competitiveness | do not raise speculatively | |
| 72 | Verify `MIR_COPY_AGGREGATE`/struct-return paths get the same dead-store/slot-hygiene treatment as scalars | Phase 2 items may not currently cover wide/aggregate objects | |
| 73 | Add `tests/tmirptrarray.c`; extend `tests/tvla.c` for any newly admitted VLA shape | | |
| 74 | Full census + full-mode validation | | |
| 75 | VLA-specific `-fstack-check` safety net: diff full assembly of old-vs-new compilers built with `-stack 512 -fstack-check` directly, for every `mir.has_vla` function touched | the census tool never builds with `-fstack-check` (documented blind spot) | Mandatory, not optional. |
| 76 | Milestone checkpoint | | |

### Phase 8 — Large-CFG scaling & parked-selector audit (Items 77–84)

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 77 | Re-measure `cfg-block-count` fallbacks after Phases 1–4 shrink block counts broadly; raise the 64-block ceiling only in small measured increments | | |
| 78 | Audit `mir_try_emit_general_rollout` and `mir_try_emit_home_cfg_rollout` | both exist in `dcc_mir.c` but are reachable only via diagnostic env vars (`DCC_MIR_GENERAL_CANDIDATES`, `DCC_MIR_EMIT_GENERAL`, etc.), **not** the production `mir_try_emit_z80` dispatcher | Check git history for why they were parked before deciding to promote or retire. |
| 79 | If promotable, add to the production chain behind the same cost gate as every other selector | never bypass the size/instruction comparison | |
| 80 | If not promotable, remove the diagnostic-only code paths in a dedicated cleanup commit | confirm via full census + `DCC_MIR_REQUIRE_COMPLETE` that nothing regresses | Reduces `dcc_mir.c`'s ~9,800 lines with no behavior change. |
| 81 | Measure compile-time scaling for the largest CFGs after any block-count ceiling increase | skill completion criterion: "large CFG compile time is bounded" | |
| 82 | Add a compile-time regression benchmark if none exists | | |
| 83 | Full census + full-mode validation | | |
| 84 | Milestone checkpoint | wide safety net | |

### Phase 9 — Cross-cutting hardening & multi-platform (Items 85–92)

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 85 | Run the full census with `-fstack-check` explicitly passed for every item landed in Phases 1–8, not only VLA ones | closes the blind spot broadly | |
| 86 | Grep every new helper added in Phases 1–8 for duplicated slot/frame-size accounting; consolidate to one predicate function each | repeat of the Item 19 lesson, applied plan-wide | |
| 87 | Run `scripts/rtl-iy-safety.py` and `scripts/audit-runtime-coverage.py` | confirm no MIR change introduced an IY reference or uncovered runtime symbol | |
| 88 | Run the full suite on Windows/MSVC and Linux/GCC hosts (or CI) | promote from "optional" (per the retired progress doc) to required, given the larger scope of this plan | |
| 89 | Run the extended C-conformance corpus (`-Extended`, 196 tests) at every phase boundary, not only at the end | | |
| 90 | `git diff --check` + `git fsck --no-progress --no-dangling` before any milestone push | | |
| 91 | Update `.github/skills/mir-migration/SKILL.md` with any newly-discovered acceptance-barrier categories or discipline rules | | |
| 92 | Milestone: full merge-readiness report with Phases 1–8's real numbers | wide safety net | |

### Phase 10 — Closing sweep & legacy-retirement readiness (Items 93–100)

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 93 | Full fresh census; final coverage fraction and complete remaining fallback-reason table | | |
| 94 | Document, per remaining fallback reason, whether it's an intentional gate or genuinely unimplemented | skill completion criterion 1: no unexplained fallback | |
| 95 | Assess whether a MIR-required emission mode can now pass correctness across the full corpus | | |
| 96 | If not ready, write the specific blocking classes into the handoff for the next plan | do not silently drop them | |
| 97 | Measure whether skipping legacy capture/replay for long-track-record MIR functions saves `dccmake` wall time | compile-time optimization only, no output-behavior change | |
| 98 | Final whole-corpus performance summary (peep + nopeep) vs. the pre-Phase-1 baseline | confirm zero net regressions and highlight the largest wins | |
| 99 | Update `perf_baselines.csv` for every genuinely improved row, with the full-mode diff attached | | |
| 100 | Write the closing status report and hand off for a possible next 100-item plan, using this same "fresh census first" discipline | | |

## Universal per-item execution playbook

Follow for every item above.

1. **Orient.** Check `git status --short` and `git log -3 --oneline`; only
   unrelated known files (`dcc-regalloc-review.docx`) may be dirty.
2. **Confirm baseline.** `python3 scripts/mir-migration-census.py --output
   build/item<N>-before.tsv --compare build/mir-plan-fresh-before.tsv`
   (first item: compare against `build/fresh-census.tsv`). Diff must be
   empty before touching code.
3. **Investigate and confirm the premise is real** for the named
   app(s)/function(s) — the table's discriminator is a starting
   hypothesis, not a verified fact. Use `DCC_MIR_SELECT_REPORT=1`,
   `DCC_MIR_REPORT=1 DCC_MIR_FUNCTION=<fn>`, and read the relevant code in
   `dcc_mir.c` by MIR opcode or selector function name. If the premise
   doesn't hold, log it as "verified already satisfied" and move on —
   don't force a change (skill rule: don't widen a gate speculatively).
4. **Implement** the minimal, targeted, structural change (skill rule 6 —
   never an app/function-name exception).
5. **Rebuild.** `sh src/dcc/build-dcc.sh`.
6. **Full census with regression gate.** `python3
   scripts/mir-migration-census.py --output build/item<N>-after.tsv
   --compare build/mir-plan-fresh-before.tsv --fail-on-regression`. List
   every app with any changed row — that's the exact validation set for
   step 7.
7. **Runtime-validate every changed app.** `pwsh ./scripts/runall.ps1
   -Apps <changed-apps> -Mode full -RunTimeout 20`. Zero regressions
   (any metric, peep or nopeep). Bisect with
   `DCC_MIR_FORCE_FALLBACK_FUNCTION` or a `git worktree` (never `git
   stash` for a before/after binary comparison) if something regresses;
   fix with the narrowest possible guard.
8. **VLA/stack-check safety net** whenever an item shrinks bytes for any
   `mir.has_vla` function: diff full `.MAC` output built with `-stack 512
   -fstack-check` directly between old and new compilers.
9. **Wide safety net** for items marked as such, or any item touching
   code reachable by more than a handful of apps: `pwsh
   ./scripts/runall.ps1 -Mode full -Extended -RunTimeout 20`, 0 failures.
10. **Promote the baseline.** `cp build/item<N>-after.tsv
    build/mir-plan-fresh-before.tsv`.
11. **Document.** Append one entry to this file's Execution Log (create
    the section on item 1) describing what changed, why, bugs
    found/fixed, census delta, validation results, and new coverage
    fraction.
12. **Commit and publish.** See below — **do not push without explicit
    per-session confirmation of the target remote.**

## Commit and publish workflow — remote decision required before executing Item 1

This repository has two remotes:

| Remote | URL | What it is |
| --- | --- | --- |
| `origin` | `https://github.com/gloveboxes/dcc.git` | This workspace's own fork/remote — the current branch (`perf/unified-regalloc`) already tracks `origin/perf/unified-regalloc`. |
| `upstream` | `https://github.com/davidly/dcc.git` | A **different GitHub account's** repository. |

The user's request said each migration should "commit and publish to the
upstream repo." Per this session's operational-safety rules, pushing to a
shared/third-party repository is an action that requires explicit
confirmation, and this session cannot verify write access to
`davidly/dcc`. **Recommendation, pending confirmation:**

- Commit every item locally on the current branch, one commit per item,
  using messages like `MIR: <imperative summary> (Plan item <N>)`.
- Push to **`origin <branch>`** (the user's own remote) after each item
  passes validation — this is the safe default and matches the existing
  tracking branch.
- Do **not** push directly to `upstream` (`davidly/dcc`) without explicit,
  separate confirmation for that specific push. If the real intent is to
  contribute this work to the upstream project, the normal path is a pull
  request opened from `origin` against `upstream/main`, not direct pushes
  to someone else's repository.

Confirm the intended remote/branch before Item 1's commit-and-publish step
runs.

```sh
git add src/dcc/dcc_mir.c mir-migration-plan-100.md
git commit -m "MIR: <summary> (Plan item <N>)"
git push origin perf/unified-regalloc   # NOT upstream, pending confirmation
```

Never stage `dcc-regalloc-review.docx` or anything under `build/` —
verify with `git check-ignore -v build/item<N>-before.tsv` if unsure.

## Multi-person coordination (if split across contributors)

Assign one owner per phase (phases are the natural fallback-class
boundaries here). Record, per the skill's handoff template: owner, base
commit, phase, candidate discriminator, before snapshot, hypothesis,
focused validation command, and files expected to change. Avoid two
owners editing `mir_try_emit_spilled_scalar_cfg` or
`mir_emit_scalar_compare` simultaneously — Phases 1–2 both touch these
heavily and should be sequenced, not parallelized, even across
contributors.

## Execution Log

_(append one entry per completed item, in table order, starting with Item
1)_

- **Item 1** (2026-07-30): Fused single-use scalar comparisons directly into
  their consuming `MIR_BRANCH_FALSE` inside `mir_try_emit_spilled_scalar_cfg`.
  Added `mir_binary_is_fusable_comparison()` / `mir_emit_fused_comparison_branch()`
  (`dcc_mir.c`, before `mir_try_emit_spilled_scalar_cfg`): when a comparison's
  result has exactly one use and that use is the immediately following
  `MIR_BRANCH_FALSE`, test the flags from `sbc hl,de` directly and jump to the
  branch's true/false targets, instead of materializing an explicit 0/1 via
  `mir_emit_scalar_compare`, spilling it to a backend slot, and reloading it
  for the branch's own `ld a,h / or l` test.
  Coverage 165/2319 (7.12%) -> 170/2319 (7.33%), 5 newly MIR-emitted functions
  (`check_s` in tesc/tstr3/tsyntax, `check_str` in tscanf, `check` in
  tsprintf), 0 regressions (`--fail-on-regression` clean). Focused validation
  (fint, tcrcfix, tesc, tscanf, tsprintf, tstr3, tsyntax, ttypesr, tvla) all
  passed peep+nopeep with 0 regressions / 5 improvements (perf baselines
  promoted); full fast-mode corpus (310 apps) passed with 0 failures.

- **Item 4** (2026-07-30): Extended Item 1's fusion through a single
  intervening logical-not: `!(a OP b)` feeding a `MIR_BRANCH_FALSE` is
  exactly the branch on the complementary comparison operator (`TOK_EQ` <->
  `TOK_NE`, `<` <-> `TOK_GE`, `>` <-> `TOK_LE`), so `mir_binary_is_fusable_comparison`
  now also matches `compare -> MIR_UNARY '!' -> MIR_BRANCH_FALSE` (each with a
  single use) and returns a skip count instead of a bool;
  `mir_emit_fused_comparison_branch` takes a `negate` flag and looks the
  branch up at `compare_index + 1 + negate`. No functions crossed the
  accept/fallback threshold at this checkpoint (coverage held at 170/2319,
  7.33%) - the only census delta was 6 still-fallback functions in `tc89c2`
  shrinking by 400-3200 bytes each with 0 regressions
  (`--fail-on-regression` clean). Runtime-validated `tc89c2`
  (`-Mode full`); full fast-mode corpus (310 apps) passed with 0 failures.
  (Items 2/3 skipped for now: 2's homed-scalar-cfg sign-bias variant isn't a
  fallback source at this checkpoint and would need a shared-operand-loading
  refactor for low payoff; 3 - call-result operand fusion - folds naturally
  out of the general single-use check already in place and needs no separate
  change.)

- **Items 5 and 7** (2026-07-30, verified, no code change): confirmed via
  forced-accept assembly inspection (`/tmp/titem5.c`, an `a<b && c<d` chain
  through `spilled-scalar-cfg`) that `&&`/`||` already lower to one
  independent `compare -> MIR_BRANCH_FALSE` pair per term, each of which
  Item 1 already fuses on its own (0 `inc l` materializations in the forced
  assembly) - Item 5 needs no separate work. Item 7's "exact
  `mir_value_use_count` check, not positional scanning" is also already how
  `mir_binary_is_fusable_comparison` is written.

- **Item 8** (2026-07-30): `mir_prepare_backend_slots` was still allocating a
  live frame slot for every fused comparison's (and, for negated fusions, the
  intervening `!`'s) result, even though `mir_try_emit_spilled_scalar_cfg`
  never emits the store/load for it after Items 1/4 - a genuinely dead
  reservation, not a dead store dccpeep could ever see (nothing in the
  instruction stream to strip). Added a `mir_backend_slots_skip_fused_comparisons`
  gate (set only around the one production call site in
  `mir_try_emit_spilled_scalar_cfg`, left off for
  `mir_try_emit_general_rollout`'s diagnostic-only call so the two callers
  can't silently drift - the lesson from the earlier `__r1u`/`*` frame-slot
  drift bug) and a one-pass `fused_away[]` precompute reusing
  `mir_binary_is_fusable_comparison`. Coverage unchanged (170/2319, 7.33%,
  expected - the freed slot was never visible in emitted bytes since `(ix+d)`
  displacement bytes and the fixed 3-byte `ld hl,-N` frame adjustment don't
  vary with slot count); one still-fallback function (`tcrcfix.check_i`)
  shrank by 2 bytes from a downstream slot-reuse effect, 0 regressions.
  Runtime-validated tcrcfix; rebuilt all 6 fusion-touched apps under
  `-fstack-check` directly (the documented census blind spot) with 0 build
  failures; full fast-mode corpus (310 apps) passed.

- **Item 6** (2026-07-30, deferred): Investigated fusing a branch whose
  condition arrives via a `MIR_PHI` merging two comparison results (the
  `(cmp1) ? (cmp2) : 0` ternary-in-condition pattern), using constructed
  fixtures (`/tmp/titem6.c`, two variants) and `DCC_MIR_REPORT=1
  DCC_MIR_FUNCTION=f` MIR dumps. Confirmed the phi genuinely has a single
  use (the branch) when the ternary result isn't stored to a named
  variable, so it's fusable in principle - but doing so would require
  moving/duplicating the branch decision into each predecessor block, which
  `mir_try_emit_spilled_scalar_cfg`'s current one-pass linear emission
  architecture cannot do (it can't retroactively rewrite an
  already-emitted predecessor block). The plan's own risk assessment
  ("Lower risk than Phase 4's general phi work") was optimistic - this is
  architecturally closer to Phase 4's cross-block work than to Items 1/4's
  local peephole fusion. Deferred until Phase 4 or a dedicated cross-block
  restructuring pass; not attempted in Phase 1. (Separately confirmed: when
  the ternary result is instead stored to a named local, the phi's result
  picks up a second use from the store even if the local is never read
  afterward - a conservative-memory-semantics artifact, not a bug, and a
  case `mir_binary_is_fusable_comparison`'s single-use check is already
  correctly rejecting.)

- **Item 9** (2026-07-30): Added a `DCC_MIR_FUSE_REPORT=1` diagnostic to
  `mir_try_emit_spilled_scalar_cfg`: two new global counters
  (`mir_fuse_report_fused_count` / `mir_fuse_report_materialized_count`),
  incremented at the fusion check-and-skip site and in the still-materializing
  comparison-emission `switch` respectively, reset per function, and printed
  at the `done:` label as `; MIR fuse-report function=%s fused=%d
  materialized=%d` when either counter is non-zero. Purely diagnostic
  (gated behind `getenv()`, no change to emitted code paths) - spot-checked
  correct against `tests/tesc.c`'s `check` function (`fused=1
  materialized=0`, matching Item 1's own finding that `check`/`check_s` is a
  canonical single-fused-comparison case), then confirmed with the full
  census (`--fail-on-regression`: 0 newly/no-longer MIR-emitted) and the
  full fast-mode corpus (320 apps, including Item 10's new fixture; 311
  passed, 9 skipped, 0 failed) that adding the diagnostic changes no emitted
  bytes anywhere.

- **Item 10** (2026-07-30): Added `tests/tmirfuse.c` (named to fit CP/M's
  8.3 filename limit; originally drafted as `tmircmpfuse.c`, which
  `dccmake`/`runall.ps1` reject outright as an invalid CP/M basename) as a
  permanent regression fixture for the Item 1/4 fusion logic. Covers every
  comparison operator (`==`,`!=`,`<`,`>`,`<=`,`>=`) signed and unsigned,
  bare and negated (`!(...)`) - the two `mir_binary_is_fusable_comparison`
  return codes - across boundary values (`INT_MIN`/`INT_MAX`, 0/-1,
  unsigned wraparound via `(unsigned int)-1`), plus three functions with
  extra locals (`slt_spilled`, `nslt_spilled`, `and_chain_spilled`) intended
  to steer the selector toward `mir_try_emit_spilled_scalar_cfg`. At the
  current checkpoint every function in the file (including the `_spilled`
  ones) still falls back on `reason=text-size`, so the fixture doesn't yet
  exercise the fused path under the compiler's own selection heuristics -
  confirmed this doesn't leave the new logic actually unverified by
  building the `_spilled` functions three times with
  `DCC_MIR_FORCE_ACCEPT_FUNCTION=<name>` each, then running all three
  resulting `.COM` files under `ntvcm`: all passed
  (`tmirfuse: all tests passed`, exit 0), matching the normal (unforced)
  build's own output. Baseline added at `tests/baselines/tmirfuse.txt`.
  Validated via `runall.ps1 -Apps tmirfuse -Mode full` (pass, perf baseline
  captured) and the full fast-mode corpus (320 apps, 311 passed / 9
  skipped / 0 failed, 0 performance regressions); census
  (`--fail-on-regression`) shows 0 newly/no-longer MIR-emitted functions
  from adding the fixture itself, as expected since none of its functions
  currently cross the acceptance threshold. This fixture will start
  exercising the fused path automatically (without any further test
  changes) once a future item's selector-cost change lets
  `mir_try_emit_spilled_scalar_cfg` win on `slt_spilled`/`nslt_spilled`/
  `and_chain_spilled`.

- **Items 11–12** (2026-07-30, Phase 1 milestone): Full census re-run
  confirms coverage held at 170/2343 (7.26%) with the current checkpoint
  (Items 2/3/5/6/7 having needed no code change or being deferred, so
  Item 1 is still the sole source of the coverage jump). Full-mode
  `runall.ps1` on the 5 apps newly admitted to MIR by Item 1 (`tesc`,
  `tstr3`, `tsyntax`, `tscanf`, `tsprintf`) - all pass with 0 performance
  regressions, confirming the jump is a genuine perf win and not just
  smaller text (skill rule 4). Milestone safety net: `runall.ps1 -Mode
  full -Extended` - 320 apps (311 passed / 9 skipped / 0 failed) plus the
  extended C-standard suite (196/196 runnable cases passed, 24
  target-inapplicable skips for long long/double/GNU-extension/wchar
  reasons unrelated to this phase), 0 performance regressions, direct
  `-fstack-check` rebuilds clean. No perf-baseline changes needed beyond
  the already-captured Item 1 rows and the new `tmirfuse` app's own
  freshly-captured baseline (Item 10). Coverage snapshot and gotchas
  recorded in repo memory (`perf-optimization.md`): CP/M 8.3 filename
  limit for new test basenames, `runall.ps1 -Apps` comma-list quoting,
  and the `DCC_MIR_FORCE_ACCEPT_FUNCTION` + `dccmake` verification
  recipe for exercising a selector-rejected code path directly.
  **Phase 1 (Items 1–12) is now complete.** Proceeding to Phase 2
  (Items 13–24, backend-slot live-range hygiene / dead-store
  elimination).

- **Item 13** (2026-07-30): Skip backend-slot allocation entirely for a
  definition whose single use is the immediately following instruction with
  no intervening label/call/aliasing store. `mir_emit_virtual_store` already
  had a register-handoff fast path (`mir_can_forward_hl_to_next`) that skips
  the store/reload for such a value when it's about to feed a compatible
  next instruction directly in HL - but `mir_prepare_backend_slots` was still
  reserving a slot/frame byte for it regardless, since nothing told the
  accounting pass this value would never be spilled. Added
  `mir_backend_slot_forwardable()` (`dcc_mir.c`, next to
  `mir_prepare_backend_slots`), which re-evaluates the exact same
  `mir_can_forward_hl_to_next()` predicate used at emission time (rather than
  a second copy of the same logic - the Item 19 drift lesson) with
  `mir_emit_instruction_index` temporarily pointed at the candidate
  definition, plus a `mir_backend_slot_forward_target_is_store()` guard so a
  value whose forward target is itself a `MIR_STORE` (which
  `mir_emit_virtual_store`'s own `forward_to_store` branch still writes to
  its home slot for) keeps its slot. Also extended `mir_emit_virtual_store`'s
  `!has_slot` early-return to still arm the HL-forwarding handoff
  (`mir_forwarded_hl_value`/`mir_forwarded_hl_instruction`) for a value that
  was skipped this way, since the old early-return assumed "no slot" only
  ever meant "value is dead."

  **Bug found and fixed during validation:** the first build introduced a
  genuine regression in `tvla.c`'s `fixed_cast_bounds` (newly admitted to MIR
  by the smaller frame) - its final `MIR_PHI` result read from a bogus stack
  offset (`(ix-68)` in a 40-byte frame) because `mir_emit_spilled_phi_copies`
  writes a phi destination's value from each *predecessor's* jump/branch
  instruction, with `mir_emit_instruction_index` left at that unrelated
  predecessor index - not from the phi's own position. Evaluating
  `mir_can_forward_hl_to_next()` there checked the wrong "next instruction"
  entirely and wrongly elided the phi destination's slot. Fixed by excluding
  any `MIR_PHI` destination from `mir_backend_slot_forwardable()` unconditionally
  (phi destinations always keep a real slot); confirmed correct-again
  assembly with a direct before/after `.MAC` diff for the function, and the
  full `--fail-on-regression` census gate now passes clean at 171/2343 (only
  `tbool.bool_identity` newly admitted; `fixed_cast_bounds` no longer crosses
  the threshold at this checkpoint since its slot count is unchanged once phi
  destinations are excluded - not a "verified already satisfied" case, but a
  real premise (Item 13's slot-skip) that needed the phi carve-out before it
  was actually safe).

  Coverage 170/2343 (7.26%) -> 171/2343 (7.30%), 1 newly MIR-emitted function
  (`tbool.bool_identity`). Runtime-validated `tbool` (`-Mode full`): 0
  regressions, 2 genuine improvements (peep 57,444 -> 57,356 cycles, -0.15%;
  nopeep 60,173 -> 60,065 cycles, -0.18%; sizes unchanged) - perf baseline
  updated for `tbool` only. Wide fast-mode safety net (`-Mode fast`, 320
  apps): 311 passed / 9 skipped / 0 failed. Baseline snapshot promoted to
  `build/mir-plan-fresh-before.tsv`.

- **Item 14** (2026-07-30, deferred): Investigated extending Item 13's
  slot-skip across a `MIR_CALL` boundary for a value consumed only as a call
  argument. `mir_emit_virtual_store` already has a mechanism for exactly this
  case - `mir_call_argument_cache_target()`/`mir_emit_cached_call_argument()`,
  which moves a value into `BC` (or `DE:HL` via `exx` for wide values)
  instead of its home slot when its only remaining use is a later
  `MIR_ARG`+`MIR_CALL` pair. However, unlike Item 13's HL-forward predicate
  (`mir_can_forward_hl_to_next`, a pure function of the MIR instruction
  stream), `mir_call_argument_cache_target()`'s own gate -
  `mir_cached_call_value >= 0` / `mir_cached_wide_call_value >= 0` - depends
  on whether an *earlier, not-yet-consumed* cached value from a different
  definition is already occupying that same register pair at the moment of
  emission: a dynamic, emission-order-dependent property that a one-pass
  static scan over `first[value]` (as `mir_prepare_backend_slots` performs)
  cannot evaluate without re-simulating the emitter's cache-occupancy state
  transition by transition. Reusing this predicate directly for a
  slot-allocation decision - the same "one predicate, no drift" approach that
  made Item 13 safe - is therefore not available for this case, and
  duplicating that stateful simulation inside the accounting pass is exactly
  the two-divergent-paths hazard the repo's own Item 19 discriminator warns
  about (documented root cause of a prior stack-corruption bug). A
  provably-safe subset exists in principle - skip the slot only when static
  analysis proves at most one BC-cacheable and one wide-cacheable candidate
  exists anywhere in the function, so the runtime occupancy check could never
  actually trigger - but implementing and proving that whole-function
  uniqueness scan is materially more machinery than a single item's minimal
  edit, for a case this codebase's current selectors already handle
  correctly (just with a slot that goes unused, not a correctness bug).
  Deferred pending a dedicated whole-function occupancy-safety pass; not
  attempted here. No code change; coverage unchanged (171/2343, 7.30%).

- **Item 15** (2026-07-30): Extended Item 13's forwarding to tolerate a
  `MIR_LABEL` sitting between a value's definition and its single consuming
  instruction, when that label has exactly one CFG predecessor (i.e. it is
  not a real merge point - just a name given to a position for an unrelated
  reason, such as a `goto` target reached from nowhere else or tooling that
  always labels certain positions). Added `mir_label_predecessor_count()`,
  which sums `successors[]`/`successor_count` matches across every
  instruction - the same CFG arrays `mir_verify_and_dump()` already builds
  for every function and that liveness/allocation already trust - and
  factored a single shared `mir_forward_skip_target(instruction)` helper that
  skips NOPs and, at most once, one such single-predecessor label. Rewired
  both the accounting-time predicate (`mir_can_forward_hl_to_next`,
  `mir_backend_slot_forward_target_is_store`) and the two emission-time
  "what's next" lookups in `mir_emit_virtual_store` to call this one helper,
  so the Item 19 "one predicate, no drift" discipline extends to the new
  label case exactly as it already did for NOPs. Skipping more than one label
  is deliberately unsupported - it would require reasoning about a chain of
  merges instead of a single, locally-verifiable non-merge point.

  A combined attempt that also completed Item 14's call-argument-cache
  slot-skip (`mir_backend_slot_call_cacheable`, tracking per-lane "busy
  until" high-water marks with isolated save/restore probes of the real
  cache globals, plus the `MIR_PHI` exclusion guard and the matching
  `mir_emit_virtual_store_wide` fix) was implemented, built clean, and
  committed, but its regression-gated census showed a real correctness-class
  regression - `cint.if_stmt` flipped from `accepted` to a categorical
  `inline-substitution` fallback (a structural gate, not a size threshold)
  despite *smaller* generated code - across a 277-app blast radius. This is
  exactly the emission-order-dependent occupancy hazard the original Item 14
  deferral (above) warned about materializing in practice. That combined
  commit was reverted immediately; Item 14 stays deferred pending the
  dedicated whole-function occupancy-safety pass already described there.
  Item 15 alone (this entry) carries no such risk - it only affects the
  `mir_can_forward_hl_to_next` HL-forwarding predicate already proven safe
  for Item 13, extended to one additional, locally-verifiable case.

  Rebuilt clean (no new warnings). Census (`--fail-on-regression`) for Item
  15 alone against the Item 13 baseline: 0 newly/no-longer emitted
  (171/2343, 7.30% - unchanged, since this item only improves
  *already-emitted* functions' generated code rather than admitting new
  ones), 19 apps with changed metrics
  (`tbcint, tbcregno, tc89comp, tc89size, tc99apar, tc99scpe, tcrcfix,
  tctxflt, tenumfsm, tforinc, tkandr, tmatbit, tnarrow, tnestfor, tpeepal,
  tptrixld, treg, tregnarw, tvla`). Runtime-validated all 19 in `-Mode full`:
  0 regressions, cycle counts/sizes unchanged (this item's code-shape changes
  did not move the needle on these particular apps' hot paths) - confirmed
  via `-UpdatePerfBaseline` producing no diff. Wide fast-mode safety net
  (`-Mode fast`, 320 apps): 311 passed / 9 skipped / 0 failed, clean.
  Baseline snapshot promoted to `build/mir-plan-fresh-before.tsv`. No perf
  baseline changes needed (no genuine improvement or regression detected).

  Note: this session's working tree was intermittently reset (`git reset
  --hard HEAD`) by an external, unidentified process while this item was in
  progress, twice silently discarding uncommitted edits before they could be
  committed (visible in `git stash list`/`git reflog` as recurring "WIP on
  perf/unified-regalloc" / "reset: moving to HEAD" entries). Both the
  originally-lost Item 15 work and a separately-lost Item 14 attempt were
  recovered from the stash list and re-validated from scratch; all commits
  in this item's history were made immediately after each successful build
  to minimize exposure to that hazard.

- **Item 16** (2026-07-30, verified already satisfied): Investigated the
  `check_s`-class dead-store case cited as evidence for this item (the
  skill's documented root-cause finding: a compare result gets
  spilled/reloaded twice, and dccpeep's same-block redundant-reload pass
  only removes one round-trip). Traced `check_s` in `tests/tstr3.c`/
  `tests/tsyntax.c` directly: `call __scmp` / `pop bc` / `pop bc` / `ld
  (ix-2),l` / `ld (ix-1),h` / `ld l,(ix-2)` / `ld h,(ix-1)` / `push hl`. This
  store *is* read - exactly once, immediately afterward - so it is not the
  "written but never read again anywhere" dead store Item 16 describes; it
  is a call-result HL-forwarding gap, structurally identical to what Item 14
  investigated and deferred (`mir_can_forward_hl_to_next` unconditionally
  rejects any `MIR_CALL`/`MIR_CALL_AGGREGATE`-defined value, precisely to
  avoid assuming no intervening register-clobbering code exists between the
  call and the store without the same occupancy-safety proof). For the
  literal "genuinely dead value" case Item 16 asks for,
  `mir_prepare_backend_slots` already has this covered: `if (last[value] <=
  first[value] || ...) continue;` skips slot allocation for any backend-slot
  value whose `last[value]` (updated for every `src1`/`src2`/`MIR_CALL`
  argument/`MIR_PHI` source use) never advances past its own definition
  instruction - i.e. a value stored but never read anywhere in the function
  already gets no slot at all, predating this session. No function in the
  runnable corpus was found where a currently-emitted value violates this.
  No code change; coverage unchanged (171/2343, 7.30%).

- **Item 17** (2026-07-30, verified already satisfied): Item 17 asks to
  generalize Item 16 with a per-object backward-liveness pass over the whole
  function, reusing the Items 11/12 dead-promoted-local-store infrastructure
  as a model. That infrastructure already exists and already performs
  exactly this: commit `de0174f` ("MIR: eliminate dead promoted-local stores
  via per-object backward liveness") added per-object backward liveness for
  partially-promoted objects (objects with zero loads anywhere were already
  unconditionally elided via `mir_object_is_fully_promoted`; that commit
  extended elision to objects with *some* real loads but a store that is
  dead on every live path from it). Combined with Item 16's backend-slot
  value-level dead-value check (above), both the object-store and the
  virtual-value domains already have backward-liveness-based dead-store
  elision. No further generalization identified; no code change; coverage
  unchanged (171/2343, 7.30%).

- **Item 18** (2026-07-30, verified already satisfied): `mir_emit_spilled_phi_copies`
  already guards each phi-destination copy with `if (!mir_value_has_use(phi->dst))
  { ++instruction; continue; }` before emitting the source-load/destination-store
  pair, where `mir_value_has_use` scans the whole function for any `src1`/`src2`/
  call-argument use - the same whole-function liveness definition Items 13-17
  rely on. A phi destination already proved dead is therefore already skipped
  entirely, not just its backend slot. No code change; coverage unchanged
  (171/2343, 7.30%).

- **Item 19** (2026-07-30, audited, no code change): Audited every skip
  predicate `mir_prepare_backend_slots`'s accounting loop consults -
  `mir_call_only_constant`, `mir_multiply_by_small_constant`,
  `mir_load_is_single_call_argument`, `mir_binary_is_fusable_comparison`
  (via `fused_away`), and `mir_backend_slot_forwardable` (Item 13/15) - and
  confirmed each is the *same* function called again at its corresponding
  emission site (`mir_emit_virtual_load`/`mir_emit_virtual_store`,
  `mir_emit_fused_comparison_branch`, etc.), never a re-derived duplicate of
  the same logic. This is the discipline the repo's own Item 19 lesson
  requires (a prior stack-corruption bug traced to an accounting pass and
  its emission-time counterpart drifting apart). No violation found across
  the current predicate set; no code change needed. This audit itself is the
  deliverable for this item; coverage unchanged (171/2343, 7.30%).

- **Item 20** (2026-07-30): Added `DCC_MIR_SLOT_REPORT=1`, printing
  `; MIR slot-report function=<name> requested=<N> assigned=<M>` per
  function from `mir_prepare_backend_slots` - `requested` counts every value
  reaching its own first-definition point (one candidate per
  `first[value] == i` iteration), `assigned` counts only those that make it
  past all of Items 13-18's dead-value/forwarding/fusion/reuse skip
  predicates to receive a real frame slot (fresh or operand-reused). A large
  requested/assigned gap flags a function as still having a lot of slot
  traffic elided already (a healthy sign, not a problem); a *small* gap on a
  function whose captured/generated byte counts are close is the signal this
  item exists to surface for future prioritization. Diagnostic-only, no
  behavior change: rebuilt clean, census identical to baseline (171/2343,
  0 apps changed, exit 0), wide `-Mode fast` safety net (320 apps): 311
  passed / 9 skipped / 0 failed.

- **Item 21** (2026-07-30): Added `tests/tmirslot.c`, a permanent regression
  fixture covering the scenarios Items 13/15/16/17/18 elide slot traffic
  for: `immediate_use` (call result consumed by the very next instruction),
  `forward_across_label`/`forward_across_label` variants (Item 15's
  single-predecessor label forwarding, both branches taken and not taken),
  `forward_into_store` (value forwarded straight into a local with no
  intervening use), `dead_store_elision` (a local written twice but never
  read after its final write - Item 16/17's dead-store/backward-liveness
  elision), and `phi_partial_dead` (a phi destination dead down one
  incoming edge but live down the other, to guard Item 18's dead-copy skip
  against misfiring on the live edge). All expected values were
  cross-checked against a native `clang` build of the same source before
  wiring the dcc baseline. Added `tests/baselines/tmirslot.txt` and
  captured initial peep/nopeep perf baselines via `-UpdatePerfBaseline`
  (first-time capture, not a movement of an existing row). Validated:
  `runall -Apps tmirslot -Mode full` (fast+nopeep, both pass, stack-check
  enabled), then a wide `-Mode fast` safety net across the whole corpus
  (321 apps total incl. the new fixture: 312 passed / 9 skipped / 0
  failed, dccpeep fixtures 17/17 passed, diagnostics passed).

- **Item 22** (2026-07-30): Milestone-tier validation of the whole Phase 2
  batch (Items 13/15/16/17/18/20/21) against `build/item13-after.tsv`, the
  last fully-validated pre-Phase-2 baseline. Full census
  (`--fail-on-regression`, exit 0): coverage 171/2353 (7.27%; the `+10`
  function count vs. the 2343 prior total is `tmirslot.c`'s own 10 test
  functions), 0 newly/no-longer MIR-emitted, 20 apps with census metric
  changes (byte-count deltas from Item 15's label-forwarding and Item 20's
  diagnostic-only counters), **0 apps flagged as requiring runtime
  validation** by the census tool itself. Ran the milestone tier anyway
  per the skill's "before merging" guidance: whole-corpus
  `runall -Mode full` (fast+nopeep, 321 apps): 312 passed / 9 skipped / 0
  failed, dccpeep fixtures 17/17 passed, diagnostics passed, performance
  passed (no regressions). Promoted `build/item22-full.tsv` to
  `build/mir-plan-fresh-before.tsv` as the new rolling baseline for
  subsequent items.

- **Item 23** (2026-07-30): Searched for a "largest-yield app" to profile
  dynamically per skill rule 4. Diffed every row of `build/item13-after.tsv`
  against `build/item22-full.tsv`: every byte-count delta this phase
  produced (Item 15's label-forwarding candidate-size shrink, ~50-60 bytes
  each across `tbcint`, `tbcregno`, `tc89comp`, `tc89size`, `tc99apar`,
  `tc99scpe`, `tcrcfix`, and others) belongs exclusively to functions still
  in `fallback text-size` - i.e. functions whose *emitted* code is still
  the captured legacy replay, unaffected by the smaller MIR candidate that
  was measured but rejected. Zero functions changed `result` from
  `fallback` to `mir` or vice versa, and zero already-`mir`-accepted
  functions had any generated-byte delta at all (confirmed by an exact
  script diff, 0 matches). This matches the census tool's own verdict from
  Item 22 (`apps requiring runtime validation: 0`): there is no
  behavioral or performance difference for any binary this phase produced,
  so there is no candidate app/function pair to profile with
  `dccprof`/cycle counts. Documented as a deliberate no-op rather than
  fabricating a profiling run against unchanged binaries; skipped, no
  regression risk.

- **Item 24** (2026-07-30): Phase 2 milestone checkpoint. No source changes
  occurred between Item 22's milestone validation and this checkpoint, so
  Item 22's whole-corpus `runall -Mode full` run (321 apps: 312 passed / 9
  skipped / 0 failed, dccpeep fixtures 17/17, diagnostics passed,
  performance passed) stands as the closing wide safety net for this
  phase; re-running it against byte-identical binaries would have added
  no information. Per Item 23, no MIR-accepted function's generated code
  changed this phase (Item 15's forwarding only shrank rejected-candidate
  sizes on already-`fallback` functions), so there is nothing to promote
  into `tests/perf_baselines.csv` beyond `tmirslot.c`'s own first-time
  capture (Item 21) - no existing baseline row is moved.
  **Phase 2 summary (Items 13-24):** coverage held at 171/2353
  (7.27%, +10 functions vs. the 2343 pre-phase total from `tmirslot.c`'s
  own fixtures); one real code change survived (Item 15's label-forwarding
  extension of Item 13, `51eb33a`); one attempted combination (Item 14's
  call-argument-cache slot skip) was implemented, empirically found to
  regress `cint.if_stmt`'s acceptance category, and reverted (`3bfb0c4` /
  `d0b1cab`) - Item 14 remains deferred pending a provably-safe subset
  design; Items 16-19 were audited/verified already satisfied by existing
  infrastructure (no code change); Item 20 added a diagnostic
  (`DCC_MIR_SLOT_REPORT`); Item 21 added a permanent regression fixture;
  Items 22-23 closed the phase with a clean milestone census/full-mode run
  and a documented absence of a dynamic-profiling candidate. An external,
  unidentified process was found to be periodically resetting/stashing
  this working tree mid-session (see Item 15's log entry) - all commits in
  this phase were made immediately after each build to minimize exposure
  to that hazard, and no work is known to have been permanently lost
  (recovered instances are noted inline). Phase 2 is complete; Phase 3
  (Items 25-34) was out of scope for this task and was not started.




