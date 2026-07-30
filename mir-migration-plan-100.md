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
| 25 | Emit `ld a,h / or l` directly for compare-with-constant-zero at the Phase-1 fusion site | pre-peep evidence in the `check_s` case study (`push/ld hl,0/ex de,hl/pop hl/or a/sbc hl,de`) | done - see Execution Log |
| 26 | Add an 8-bit-range compare-with-small-constant fast path (`cp` byte form) instead of full 16-bit `sbc hl,de` | | deferred - see Execution Log |
| 27 | Add a sign-bit test (`bit 7,h`) fast path for `<`/`>=` against constant 0 | | done - see Execution Log |
| 28 | Re-check whether `%`/`/` by a power of two reaching `spilled-scalar-cfg` already gets `dccpeep`'s `pass_const_divmod_helpers` treatment post-emission | may be "verified already satisfied" — check before implementing (skill rule 1) | verified already satisfied - see Execution Log |
| 29 | Extend call-argument rematerialization to a constant operand consumed by a post-call comparison/binary op | current rematerialization only covers "single-use call arguments" per the (retired) progress doc | investigated, found inert - see Execution Log |
| 30 | Audit `mir_mul_const_fast_path_eligible` for additional profitable multiplier shapes the fresh census surfaces | | done - see Execution Log |
| 31 | Add an `inc (ix+n)`/`dec (ix+n)` in-place fast path for ±1 updates to a spilled slot | mirrors the legacy backend's already-proven idiom (perf-optimization memory) | done - see Execution Log |
| 32 | Add `tests/tmirconstfast.c` covering every new fast path, clang baseline | | done - see Execution Log (file named `tests/tmirfast.c`; CP/M 8.3 name limit) |
| 33 | Full census + full-mode validation | | done - see Execution Log |
| 34 | Milestone checkpoint | | done - see Execution Log |

### Phase 4 — CFG shape hygiene (Items 35–44)

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 35 | Collapse a label immediately followed only by an unconditional jump into a direct predecessor retarget | `and_expr` case study's `L3575: jp L3573` | done - see Execution Log |
| 36 | Generalize to transitive jump-to-jump chains | bound iteration to avoid cyclic label chains | done - see Execution Log |
| 37 | Remove a `MIR_LABEL` with zero remaining predecessors after Items 35–36 | | deferred - see Execution Log (breaks object-phi promotion's block-identity lookup) |
| 38 | Reuse an already-live value across a two-predecessor join when both predecessors define it identically, instead of spilling | build on the loop-header object-phi work already landed (commits `6144885`, `1ffeb1e`, `729bc11`) | done - see Execution Log |
| 39 | Extend Item 38 to a join where only one predecessor differs and the other is a plain fallthrough | | deferred - see Execution Log (real byte-cost win, but an asymmetric-branch-cost runtime regression a static gate can't catch) |
| 40 | Re-run the fresh census after Items 35–39 to see whether `cfg-block-count`/`cfg-backedge` fallbacks move to accepted purely from lower block counts | | done - see Execution Log (no change; same 5 functions remain fallback) |
| 41 | Add `DCC_MIR_PHI_REPORT=1`: phi-join reuse hits/misses | | done - see Execution Log |
| 42 | Add `tests/tmircfgshape.c`: nested if/else value joins + loop continue-block collapsing, clang baseline | | done - see Execution Log (file named `tests/tphijoin.c`; CP/M 8.3 name limit) |
| 43 | Full census + full-mode validation | | done - see Execution Log |
| 44 | Milestone checkpoint | wide safety net | done - see Execution Log (Phase 4 complete) |

### Phase 5 — Loop competitiveness generalization (Items 45–56)

Skill risk ordering places loops/backedges above only large-CFG/inlining
— treat this phase with the most caution so far.

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 45 | Re-scope: which functions still fail on `cfg-backedge` after Phases 1–4 | mandatory fresh census, not the old count | Done - major finding: the 4 loop selectors were dead code in production (dispatch-wiring gap, not selector quality) |
| 46 | Generalize `mir_try_emit_countdown_loop` to a runtime bound already resident in a parameter/local | | Done (reframed) - fixed the dispatch-wiring gap itself: retry the 4 existing loop selectors when `cfg-backedge` is the only blocking reason. `bcd_div10` now MIR-accepted (matched `mir_try_emit_unsigned_division_loop` exactly, no generalization needed). `lres`/`loop_header_phi` still fall back - different, larger gaps (SC_LOCAL loop var, ascending-counter-vs-parameter-bound shape) deferred to later items |
| 47 | Generalize `mir_try_emit_accumulator_loop` to more than one independently BC/DE/IY-allocatable accumulator | | deferred - see Execution Log (no 2-accumulator function exists in the re-scoped census; only 2 cfg-backedge functions remain and neither is multi-accumulator) |
| 48 | Extend `mir_try_emit_unsigned_division_loop` to the signed case | if the re-scoped census shows a signed-loop fallback family | deferred - see Execution Log (no signed-division-loop fallback exists; `bcd_div10` was the only division-loop candidate and it's already unsigned and now accepted) |
| 49 | Verify the specialized loop selectors don't duplicate the materialize-then-retest comparison bug fixed in Phase 1 | | done - see Execution Log (verified clean by inspection; none of the 4 selectors call the shared compare-emission path at all) |
| 50 | Extend loop-invariant hoisting (commit `729bc11`) from the homed case to the spilled case using Phase 2's slot-hygiene machinery | | deferred - see Execution Log (no accepted spilled-scalar-cfg loop function exists to improve; the systemic ~2x spilled-cost gap makes a narrow hoisting fix unlikely to flip any current fallback) |
| 51 | Re-profile the 10 VLA power-of-two loop fallbacks under the now-cheaper general loop path | do NOT remove the gate without checking exact affected functions (skill rule 1) — likely stays a small explicit exception | deferred - see Execution Log (depends on Item 52's general loop path, which itself has no evidence-backed target; revisit only if Item 52's premise changes) |
| 52 | Add a general (non-countdown, non-accumulator) loop selector for an arbitrary acyclic loop body with a backedge | highest-risk item in the plan so far | Gate behind forced A/B profiling before any production admission. deferred - see Execution Log (Item-6-level ambiguity: no concrete corpus function needs this; the 2 remaining cfg-backedge functions each need a different, narrower, unrelated shape extension instead) |
| 53 | Dynamic profiling (`dccprof.ps1`) A/B for every function Item 52 admits | skill rule 4 — mandatory, not optional, for this item | deferred alongside Item 52 - see Execution Log |
| 54 | Add `tests/tmirloopgen.c` covering the new selector's shapes, clang baseline | | deferred alongside Item 52 - see Execution Log |
| 55 | Full census + full-mode validation | | deferred alongside Item 52 - see Execution Log |
| 56 | Milestone checkpoint including `-Extended` | wide safety net | deferred alongside Item 52 - see Execution Log |

### Phase 6 — Cost model & inline-substitution admission (Items 57–66)

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 57 | Re-measure `inline-substitution` fallbacks with the fresh census | confirm none were incidentally fixed by Phases 1–5 | done - see Execution Log (24 functions, unchanged population - `cobint` x17, `forint` x2, `tinline` x3, `tinlinfb` x1) |
| 58 | Build a static cost model (instruction count × call-site count vs. one-time expansion) | the previously-tried unconditional expansion enlarged nested bodies and didn't admit callers (documented in the retired progress doc) | deferred - see Execution Log (Item-6-level scope: MIR has zero inlining capability today; this is a new compiler pass, not a structural fix) |
| 59 | Prototype cost-gated substitution for single-call-site static-inline functions only | least risky subset first | deferred alongside Item 58 - see Execution Log |
| 60 | Extend to multi-call-site static-inline functions only when the callee body is provably small | reuse Phase 2's slot-count accounting as the size proxy | deferred alongside Item 58 - see Execution Log |
| 61 | Verify substituted bodies get the same Phase 1–4 quality treatment as top-level functions | no separate/weaker code path for substituted bodies | deferred alongside Item 58 - see Execution Log |
| 62 | Forced A/B profiling for every newly admitted inline-substitution candidate | skill: structural acceptance exceptions require profiling first | deferred alongside Item 58 - see Execution Log |
| 63 | Add `tests/tmirinline.c`: single- and multi-call-site admission | | deferred alongside Item 58 - see Execution Log |
| 64 | Full census + full-mode validation | | deferred alongside Item 58 - see Execution Log |
| 65 | Dynamic profiling for any app whose code size grows from inlining | confirm growth is still a net cycle win | deferred alongside Item 58 - see Execution Log |
| 66 | Milestone checkpoint | | deferred alongside Item 58 - see Execution Log |

### Phase 7 — Pointer-array, aggregate, and VLA structural classes (Items 67–76)

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 67 | Re-measure the `pointer-array` fallback with the fresh census | | done - see Execution Log (exactly 1 function: `tptrarr.main`) |
| 68 | Implement stride-aware lowering/emission for a declared pointer-to-array reaching `mir_has_declared_pointer_array`'s gate | design reference: legacy `ast_index_deref_pointer_array_collect` (repo memory `dcc-ast-migration.md`), adapted to MIR address/index instructions | deferred - see Execution Log (forced-accept A/B proved a real runtime regression for the sole candidate - correct but slower/bigger, skill rule 5) |
| 69 | Add a regression fixture mirroring `tests/tptrlhs.c`'s shapes through the MIR path (force-accept for validation) | | deferred alongside Item 68 - see Execution Log |
| 70 | Re-audit remaining VLA fallbacks beyond the intentional power-of-two gate; classify by whether Phases 1–6 change their cost profile | | done - see Execution Log (no distinct VLA-specific gate exists; all VLA fallbacks are the same systemic ~2x spilled-cost population documented in SKILL.md, unaffected by Phases 1–6) |
| 71 | Extend struct/aggregate size ceiling in `mir_try_emit_spilled_scalar_cfg` only after profiling shows competitiveness | do not raise speculatively | deferred - see Execution Log (no distinct ceiling exists to extend; struct/aggregate fallbacks are the same systemic gap, often worse - up to 4.3x bigger, not near-cost) |
| 72 | Verify `MIR_COPY_AGGREGATE`/struct-return paths get the same dead-store/slot-hygiene treatment as scalars | Phase 2 items may not currently cover wide/aggregate objects | deferred alongside Item 71 - see Execution Log (same root-cause population; a real fix here is the systemic spilled-path problem itself, out of this item's narrow scope) |
| 73 | Add `tests/tmirptrarray.c`; extend `tests/tvla.c` for any newly admitted VLA shape | | deferred alongside Item 68 - see Execution Log (no newly admitted shape to cover) |
| 74 | Full census + full-mode validation | | done - see Execution Log |
| 75 | VLA-specific `-fstack-check` safety net: diff full assembly of old-vs-new compilers built with `-stack 512 -fstack-check` directly, for every `mir.has_vla` function touched | the census tool never builds with `-fstack-check` (documented blind spot) | Mandatory, not optional. | N/A this phase - see Execution Log (no `mir.has_vla` function's code path changed; nothing to diff) |
| 76 | Milestone checkpoint | | done - see Execution Log |

### Phase 8 — Large-CFG scaling & parked-selector audit (Items 77–84)

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 77 | Re-measure `cfg-block-count` fallbacks after Phases 1–4 shrink block counts broadly; raise the 64-block ceiling only in small measured increments | | done - see Execution Log (same 3 functions as Item 40, unchanged: `adaint.run` 107 blocks, `cobint.exec_range` 132 blocks, `fint.prim` 95 blocks; ceiling raise deferred, see Execution Log) |
| 78 | Audit `mir_try_emit_general_rollout` and `mir_try_emit_home_cfg_rollout` | both exist in `dcc_mir.c` but are reachable only via diagnostic env vars (`DCC_MIR_GENERAL_CANDIDATES`, `DCC_MIR_EMIT_GENERAL`, etc.), **not** the production `mir_try_emit_z80` dispatcher | Check git history for why they were parked before deciding to promote or retire. done - see Execution Log (major finding: `general_rollout`/`mir_try_emit_homed_scalar_dag` is a real, better, fully dead selector; `home_cfg_rollout` is provably redundant scaffolding around an already-active selector) |
| 79 | If promotable, add to the production chain behind the same cost gate as every other selector | never bypass the size/instruction comparison | done - see Execution Log (`general-rollout` promoted: tried alongside `homed-scalar-cfg`, smaller candidate wins; 0 regressions, 11 real measured cycle improvements across 6 apps) |
| 80 | If not promotable, remove the diagnostic-only code paths in a dedicated cleanup commit | confirm via full census + `DCC_MIR_REQUIRE_COMPLETE` that nothing regresses | Reduces `dcc_mir.c`'s ~9,800 lines with no behavior change. done - see Execution Log (`mir_try_emit_home_cfg_rollout` and its 2 diagnostic call sites removed; census confirmed byte-identical before/after for every function it used to match) |
| 81 | Measure compile-time scaling for the largest CFGs after any block-count ceiling increase | skill completion criterion: "large CFG compile time is bounded" | N/A - see Execution Log (no ceiling was raised; Item 77 deferred the raise) |
| 82 | Add a compile-time regression benchmark if none exists | | N/A alongside Item 81 - see Execution Log |
| 83 | Full census + full-mode validation | | done - see Execution Log |
| 84 | Milestone checkpoint | wide safety net | done - see Execution Log |

### Phase 9 — Cross-cutting hardening & multi-platform (Items 85–92)

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 85 | Run the full census with `-fstack-check` explicitly passed for every item landed in Phases 1–8, not only VLA ones | closes the blind spot broadly | done - added `--extra-args` to the census script; see Execution Log |
| 86 | Grep every new helper added in Phases 1–8 for duplicated slot/frame-size accounting; consolidate to one predicate function each | repeat of the Item 19 lesson, applied plan-wide | done - `mir_current_frame_bytes()` extracted; see Execution Log |
| 87 | Run `scripts/rtl-iy-safety.py` and `scripts/audit-runtime-coverage.py` | confirm no MIR change introduced an IY reference or uncovered runtime symbol | done - both clean; see Execution Log |
| 88 | Run the full suite on Windows/MSVC and Linux/GCC hosts (or CI) | promote from "optional" (per the retired progress doc) to required, given the larger scope of this plan | done - added a `windows-latest` CI matrix leg alongside `ubuntu-latest`; see Execution Log |
| 89 | Run the extended C-conformance corpus (`-Extended`, 196 tests) at every phase boundary, not only at the end | | done - already standard practice every phase this session (see Items 45-84's milestone runs); reconfirmed here |
| 90 | `git diff --check` + `git fsck --no-progress --no-dangling` before any milestone push | | done - both clean; see Execution Log |
| 91 | Update `.github/skills/mir-migration/SKILL.md` with any newly-discovered acceptance-barrier categories or discipline rules | | done - added the dead-selector-via-env-var pattern and the frame-bytes consolidation discipline note |
| 92 | Milestone: full merge-readiness report with Phases 1–8's real numbers | wide safety net | done - see Execution Log |

### Phase 10 — Closing sweep & legacy-retirement readiness (Items 93–100)

| # | Title | Discriminator | Notes |
|---|---|---|---|
| 93 | Full fresh census; final coverage fraction and complete remaining fallback-reason table | | done - 174/2378 (7.32%); see Execution Log |
| 94 | Document, per remaining fallback reason, whether it's an intentional gate or genuinely unimplemented | skill completion criterion 1: no unexplained fallback | done - all 7 reasons classified; see Execution Log |
| 95 | Assess whether a MIR-required emission mode can now pass correctness across the full corpus | | done - not ready at 7.32% coverage; see Execution Log |
| 96 | If not ready, write the specific blocking classes into the handoff for the next plan | do not silently drop them | done - see Execution Log's closing handoff |
| 97 | Measure whether skipping legacy capture/replay for long-track-record MIR functions saves `dccmake` wall time | compile-time optimization only, no output-behavior change | deferred - evidence shows no meaningful yield at 7.32% coverage; see Execution Log |
| 98 | Final whole-corpus performance summary (peep + nopeep) vs. the pre-Phase-1 baseline | confirm zero net regressions and highlight the largest wins | done - 18 apps improved, 0 regressed; see Execution Log |
| 99 | Update `perf_baselines.csv` for every genuinely improved row, with the full-mode diff attached | | done - already current, all improvements accepted incrementally each phase |
| 100 | Write the closing status report and hand off for a possible next 100-item plan, using this same "fresh census first" discipline | | done - see Execution Log's closing entry |

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

- **Item 25** (2026-07-30): Emitted `ld a,h / or l` directly for `==`/`!=`
  comparisons against the constant 0 at the Phase-1 comparison-branch
  fusion site, instead of materializing the 0 into DE and running a full
  16-bit `or a / sbc hl,de`. Added `mir_fused_compare_is_const_zero_rhs()`
  and used it at the call site (guarded by the existing
  `mir_binary_is_fusable_comparison()` check, the single source of truth
  for whether the comparison is really about to be fused, so DE is never
  skipped for a comparison that falls through to the unfused
  `mir_emit_scalar_compare` path) to skip the `push hl / ld hl,0 / ex de,hl
  / pop hl` DE-load entirely when the right-hand operand is a zero
  constant, and updated `mir_emit_fused_comparison_branch` to test HL
  directly with `ld a,h / or l` in that case (2 instructions instead of 7).
  Only the right-hand-side-is-zero shape (`x == 0` / `x != 0`) is handled;
  the left-hand-side case (`0 == x`) still loads HL with the constant
  first via the unconditional src1 load, so no benefit is available there
  without reordering operand evaluation, which this item does not attempt.
  Rebuilt clean. Census (`--fail-on-regression`): 0 newly/no-longer
  emitted (171/2353, 7.27% - this changes generated code within
  already-classified categories, not acceptance), 111 apps with changed
  metrics, 5 flagged for runtime validation
  (`tesc, tscanf, tsprintf, tstr3, tsyntax`). Runtime-validated those 5 in
  `-Mode full`: 0 regressions. Broader spot-check across
  `cint, tc89comp, tc89decl, tmuldiv` (functions the case-study history
  flagged as `==`/`!=`-with-zero heavy) found 0 regressions and a genuine
  improvement in `tc89decl` (peep 43,810 -> 43,781 cycles/-0.07%; nopeep
  44,612 -> 44,571 cycles/-0.09%; sizes unchanged) - baseline updated for
  `tc89decl` only via `-UpdatePerfBaseline` after the full-mode proof. Wide
  fast-mode safety net (`-Mode fast`, 321 apps): 312 passed / 9 skipped /
  0 failed. Milestone `-Mode full -Extended` run: 312/321 apps passed (9
  skipped as expected), extended suite 196/196 passed, diagnostics/dccpeep
  fixtures/performance all passed.

- **Item 26 - deferred** (2026-07-30): Investigated an 8-bit-range
  `cp`-based fast path for comparisons against a small constant. By the
  time a comparison reaches `MIR_BINARY`, C's usual arithmetic conversions
  have already promoted any narrower operand to a full 16-bit int, and
  MIR does not track an operand's pre-promotion original type or a proven
  small value range surviving to that point. Proving either operand is
  guaranteed to fit in a single byte (so a single `cp`/`or`/`and` byte
  comparison could replace the full 16-bit `sbc hl,de`) would require new
  semantic/range-tracking infrastructure, not a small selector tweak -
  the same class of design gap that made Item 6 and Item 14 defer/skip
  candidates rather than in-scope edits. Deferred until such tracking
  exists; no code changed for this item.

- **Item 27** (2026-07-30): Emitted `bit 7,h` directly for signed `<`/`>=`
  comparisons against the constant 0 at the same Phase-1 fusion site as
  Item 25, instead of materializing 0 into DE and running the full
  sign-flip-and-`sbc hl,de` sequence. Added
  `mir_fused_compare_is_signed_zero_sign_test()` (rejects the unsigned
  case, where `x < 0` is always false and `x >= 0` is always true - a
  different, constant-fold opportunity this item does not attempt) and
  used it at the call site, guarded by the same
  `mir_binary_is_fusable_comparison()` check as Item 25, to skip the
  DE-load entirely. `mir_emit_fused_comparison_branch` now emits `bit 7,h`
  and branches on `z`/`nz` directly (2 instructions instead of 8: no DE
  load, no `xor 128` sign-flip pair on either operand, no `sbc hl,de`).
  Rebuilt clean. Census (`--fail-on-regression`) vs `build/phase3-before.tsv`:
  0 newly/no-longer emitted (171/2353, 7.27%), 126 apps with changed
  metrics, 5 flagged for runtime validation
  (`tesc, tscanf, tsprintf, tstr3, tsyntax`). Runtime-validated those 5 in
  `-Mode full`: 0 regressions. Broader spot-check across
  `cint, tc89comp, tc89decl, tmuldiv, tlong, tswitch, tcmp`: 0 regressions.
  Wide fast-mode safety net (`-Mode fast`, 321 apps): 312 passed / 9
  skipped / 0 failed. Milestone `-Mode full -Extended` run: 312/321 apps
  passed (9 skipped as expected), extended suite 196/196 passed,
  diagnostics/dccpeep fixtures/performance all passed.

- **Item 28 - verified already satisfied** (2026-07-30): Checked whether
  `%`/`/` by a power of two reaching `spilled-scalar-cfg` (or any other
  MIR selector) already gets `dccpeep`'s `pass_const_divmod_helpers`
  treatment post-emission, before implementing anything (skill rule 1).
  Confirmed via `DCC_MIR_FORCE_ACCEPT_FUNCTION` on a small `/`-by-4
  function: MIR emits `ld de,<const>` / `extrn __divs` / `call __divs`
  (the same call-symbol shape the legacy backend emits), and running the
  resulting `.mac` through `dccpeep` directly rewrites it to
  `ld e,2 / call __q1p` (the power-of-two shift helper) - confirming
  `pass_const_divmod_helpers`'s `__divu`/`__divs`/`__modu`/`__mods` symbol
  matching (`src/dccpeep/dccpeep.c` ~line 1636) already covers MIR-emitted
  divmod calls with no gap. No code changed for this item.

- **Item 29 - investigated, found inert** (2026-07-30): Extended call-argument
  rematerialization to also cover a constant used both as a call argument
  and consumed by a post-call comparison/binary op (`mir_call_only_constant`
  and `mir_binary_only_constant` each individually require the value have
  no *other* use, so a value satisfying both roles simultaneously fell
  through to a real backend slot today). Implemented a new
  `mir_call_and_binary_only_constant()` predicate mirroring
  `mir_binary_only_constant`'s exact eligibility rules (VLA-comparison
  exclusion, `type_size<=2`, binary-result-not-fed-to-`MIR_VLA_ALLOC`) plus
  exactly one `MIR_ARG` use, and wired it into every slot-avoidance/
  rematerialization gate alongside the two existing "only" predicates (the
  slot-assignment skip, the `MIR_CONST` definition-site skip, the deferred
  call-argument scan, and both binary-operand immediate-load fast paths).
  A full corpus census showed **zero** functions with any generated-code
  delta from this change (`build/item29-after.tsv` is byte-for-byte
  identical to `build/item27-after.tsv`) - the underlying premise does not
  arise in dcc's current architecture: the AST-to-MIR lowering never
  interns/CSEs constants, so every source-level literal occurrence
  produces its own independent `MIR_CONST` value. Two `foo(42)` /
  `x == 42` occurrences in the same function are already two separate
  values, each already covered on its own by the existing single-use
  predicates - there is no shared value for the combined predicate to
  ever match. Since the added code was fully inert (unreachable in every
  test app, unverifiable by any existing regression test, pure
  maintenance burden with no measured benefit), it was reverted rather
  than kept as speculative dead code (matching skill rule 6's "derive a
  structural predicate", not add a wired-up-but-untested path for a
  pattern the frontend doesn't produce). No net code change for this
  item; rebuilt clean after reverting and reconfirmed identical to the
  pre-item baseline.

- **Item 30** (2026-07-30): Audited `mir_mul_const_fast_path_eligible`/
  `mir_mul_const_op_count` for additional profitable multiplier shapes.
  Found that a bottom-aligned run of ones (`uv == (1 << k) - 1`, e.g.
  7, 15, 31, 63, 127, 255) is cheaper to compute as `(x << k) - x` (`k`
  doublings plus one 16-bit `sbc hl,de`) than the existing per-bit
  add decomposition (`k - 1` doublings plus `k - 1` adds) - for `k >= 4`
  this form is strictly fewer instructions, and for `k >= 7` (127, 255,
  ...) it can bring a multiplier that previously exceeded
  `MIR_MUL_CONST_MAX_OPS` (and therefore fell back to a runtime `__mulu`
  call) back under the cap into the shift/subtract fast path. Added
  `mir_mul_const_is_ones_run()` and refactored the existing per-bit
  counter into `mir_mul_const_naive_op_count()`, with
  `mir_mul_const_op_count()` (the single source of truth used by both the
  frame-slot accounting and the emission site) returning the cheaper of
  the two forms, and `mir_emit_mul_hl_const_general()` emitting the
  shift-and-subtract sequence whenever it wins. Correctness was verified
  directly: forced-accepted `x*7`, `x*15`, `x*31`, `x*63`, `x*127`, and
  `x*255` for a spread of inputs (`0, 1, 2, 3, -1, -5, 100, -100, 1000`,
  including a case that overflows 16 bits) run under `ntvcm` and compared
  byte-for-byte against Python-computed 16-bit-wrapped expected results -
  all matched exactly. Census (`--fail-on-regression`) vs
  `build/phase3-before.tsv`: 0 newly/no-longer emitted, identical to the
  Item 29 snapshot (no function in the current corpus reaches this
  selector for one of these multiplier shapes, so this is a coverage/cost
  improvement for future/broader corpora rather than a measured win in
  today's apps - the shift-and-subtract form has already been directly
  verified correct above, independent of census evidence). Focused
  `-Mode full` on `tesc, tscanf, tsprintf, tstr3, tsyntax, tmuldiv,
  tc89comp`: 0 regressions. Milestone `-Mode full -Extended` run:
  312/321 apps passed (9 skipped as expected), extended suite 196/196
  passed, diagnostics/dccpeep fixtures/performance all passed.

- **Item 31** (2026-07-31): Added an `inc (ix+n)`/`dec (ix+n)` in-place
  fast path for a bare `x++;`/`x--;` on a 16-bit non-pointer local or
  parameter, mirroring the legacy backend's `emit_incdec_sym_direct`
  (`dcc_symbols.c` ~line 1322) exactly, including its carry-checked
  byte-pair form (`inc (ix+n)` / `jp nz,done` / `inc (ix+n+1)` for
  increment; the decrement side needs an extra `ld a,(ix+n)` / `or a`
  before the low-byte `dec` because `dec` alone doesn't leave the right
  flag state to detect a low-byte borrow the way `inc`'s NZ-after-wrap
  does for the high-byte carry). Added
  `mir_binary_is_selfstore_incdec(index, *store_index)`: matches a
  `MIR_BINARY` `+`/`-` against the exact constant `1`, whose left operand
  is a 16-bit non-pointer local/parameter memory location, and whose
  result has exactly one use anywhere in the function - a plain
  `MIR_STORE` writing back to that identical location. Wired into the
  `MIR_BINARY` emission case (emits the fused sequence and skips the
  general binary-op path) and the `MIR_STORE` emission case (skips
  emitting the now-redundant store). Also added
  `mir_value_is_selfstore_incdec(value)`, a value-indexed wrapper used to
  extend `mir_prepare_backend_slots()`'s existing slot-skip condition
  chain (alongside `mir_call_only_constant`/`mir_multiply_by_small_constant`/
  `fused_away`) so the fused-away `MIR_BINARY` result no longer wastes a
  backend frame slot.
  
  While building this item's own correctness test, discovered the real
  applicability of the "exactly one use, and it's a store" condition is
  narrower than the initial hypothesis: dcc's MIR construction does local
  value numbering, so any later reference to the same source variable
  within the same basic block - or across a loop-carrying PHI merge at a
  block boundary - reuses this `MIR_BINARY`'s `dst` value directly rather
  than re-loading from memory, which gives the value a second use and
  correctly disqualifies it. That means this fusion's real hit population
  is a genuinely dead-after-increment local (e.g. `x++;` as the last
  touch of `x` before an unrelated `return`), not the more commonly
  imagined "hot loop induction variable" shape - loop-carried variables
  either stay in registers or flow through PHI nodes, and any register
  spill for them is one the allocator itself decided was necessary, not a
  redundant round-trip this fusion should remove. Documented this scope
  narrowing directly in the function's header comment for future
  readers.
  
  Correctness was verified directly: since the fused value's single
  legitimate use forbids any observation of the post-increment value
  through the normal SSA-tracked reference (by construction), the test
  takes the address of the local (`int *p = &x;`) so a `*p` read goes
  through an independent `MIR_LOAD_INDIRECT` mechanism instead of
  reusing the `MIR_BINARY`'s tracked value, exposing the true memory
  write. Force-accepted `inc_test`/`dec_test` functions built this way
  for boundary-case inputs (255→256 byte-boundary carry, `-1`→`0` and
  `0xFFFF`→`0` wraparound for increment; `256`→`255` byte-boundary
  borrow, `0`→`-1` and `1`→`0` for decrement), run under `ntvcm`: all
  matched expected 16-bit-wrapped values exactly, and the generated
  assembly was inspected directly to confirm the fused `inc (ix+n)`/
  `jp nz,...`/`inc (ix+n+1)` sequence appears with no redundant
  reload/store around it. Census (`--fail-on-regression`) vs
  `build/phase3-before.tsv`: 0 newly/no-longer MIR-emitted, 145 apps with
  metric churn (cumulative since Items 25/27/30 folded into the same
  running snapshot), 5 apps (`tesc, tscanf, tsprintf, tstr3, tsyntax`)
  flagged for runtime validation - focused `-Mode full` run on exactly
  those 5: 0 regressions, all passed. Milestone `-Mode full -Extended`
  run: 312/321 apps passed (9 skipped as expected), extended suite
  196/196 passed, diagnostics/dccpeep fixtures/performance all passed.

- **Item 32** (2026-07-31): Added a permanent regression fixture,
  `tests/tmirfast.c` (registered plan filename `tmirconstfast.c` renamed
  to fit CP/M's 8.3 filename limit), covering the semantics of every
  fast path added in Items 25/27/30/31: compare-with-zero (`eqz`/`nez`),
  signed sign-bit test (`ltz`/`gez`), the six ones-run multiplier shapes
  (`mul7`/`mul15`/`mul31`/`mul63`/`mul127`/`mul255`, including a
  16-bit-wrap case for `mul255`), and dead-after-increment/decrement
  locals (`inc_dead`/`dec_dead`, plus `inc_observe`/`dec_observe` which
  take the address of the local so the fused write can be observed
  through an independent `MIR_LOAD_INDIRECT` read rather than the
  normal SSA-tracked value). Expected values were derived with a small
  Python script modeling exact 16-bit wraparound arithmetic (not by
  hand), then cross-checked by compiling the same source under host
  `clang`: every check matched except the two cases that are
  intentionally 16-bit-target-specific (`mul255`'s wraparound case and
  `inc_observe(0xFFFF)`), which differ only because the host's `int` is
  wider than dcc's 16-bit target `int` - both are correctly documented
  as such in the source. `DCC_MIR_SELECT_REPORT=1` confirms every helper
  in the fixture still falls back to the legacy backend under today's
  text-size acceptance gate (only the trivial `side_effect` helper
  reaches MIR, via `homed-scalar-cfg`) - consistent with Items 30/31's
  finding that these fast paths' current real-world hit population is
  narrow to nonexistent; the fixture exists as a permanent safety net
  against a legacy-backend regression in this exact set of semantics,
  and will begin protecting the MIR-emitted forms too once acceptance
  criteria naturally admit functions of this shape. Added
  `tests/baselines/tmirfast.txt` and captured an initial performance
  baseline for the new app in `tests/perf_baselines.csv` (additive-only
  row, not a modification of any existing app's baseline). Ran the new
  test under `dcc`/`ntvcm` directly (`pwsh ./scripts/ma.ps1 tmirfast full`
  then `ntvcm TMIRFAST.COM`): `tmirfast: all tests passed`. Milestone
  `-Mode full -Extended` run: 313/322 apps passed (9 skipped as
  expected, tmirfast now included), extended suite 196/196 passed,
  diagnostics/dccpeep fixtures/performance all passed.

- **Item 33** (2026-07-31): Full-corpus census (`--fail-on-regression`)
  vs `build/phase3-before.tsv` (the Phase 3 starting snapshot): 0
  regressions, 172/2371 functions now MIR-emitted (7.25%, up from
  165/2319, 7.12%, at the start of Phase 3 - the +52 denominator growth
  is `tmirfast.c`'s own 18 new functions plus other unrelated corpus
  churn since the baseline was taken; the +7 numerator growth is
  `tmirfast.side_effect` (Item 32) plus functions gained across Items
  25-31's fused-compare/multiplier/incdec paths in existing apps). 1
  newly MIR-emitted function (`tmirfast.side_effect`), 0 no-longer
  emitted, 146 apps with census metric churn (cumulative since Items
  25/27/30/31 folded into the same running snapshot), 6 apps flagged for
  runtime validation (`tesc, tmirfast, tscanf, tsprintf, tstr3, tsyntax`)
  - focused `-Mode full` on exactly those 6: 0 regressions, all passed.
  Milestone `-Mode full -Extended` run: 313/322 apps passed (9 skipped as
  expected), extended suite 196/196 passed, diagnostics/dccpeep
  fixtures/performance all passed. This closes the required validation
  tier for Phase 3 (Items 25-32); Item 34 records the phase summary.

- **Item 34** (2026-07-31): Phase 3 milestone checkpoint. **Phase 3
  summary (Items 25-34):** coverage moved 165/2319 (7.12%) -> 172/2371
  (7.25%) across the phase; six independent fast-path/verification items
  landed with committed code or documented findings: Item 25 (compare-
  with-zero fast path, `9356673`), Item 26 (8-bit-range compare fast
  path, deferred - no pre-promotion range info survives to `MIR_BINARY`,
  same class of gap as Items 6/14), Item 27 (signed sign-bit-test fast
  path, `85ba6a1`), Item 28 (power-of-two divmod, verified already
  satisfied by `dccpeep`'s existing `pass_const_divmod_helpers`,
  `7624984`), Item 29 (call-argument rematerialization for constants also
  consumed by a post-call binary op, implemented then reverted after
  census proved it provably inert - dcc's AST-to-MIR lowering never
  interns/CSEs constants, so every source-level literal already gets its
  own independent `MIR_CONST` value already covered by existing
  single-use predicates; documented as a key finding for future
  candidates in this class, `5b67991`), Item 30 (shift-and-subtract fast
  path for ones-run multipliers, correctness-proven directly via `ntvcm`
  but 0 measured corpus hits today, `93443e2`), Item 31 (fused
  `inc (ix+n)`/`dec (ix+n)` for dead-after-increment locals, narrower
  real-world scope than hypothesized due to dcc's MIR value numbering -
  documented in the function's own header comment, `7e58180`), and
  Item 32 (permanent regression fixture `tests/tmirfast.c` covering all
  four items' semantics, `627b9c9`). Items 33-34 close the phase with a
  clean full-corpus census and `-Mode full -Extended` milestone run (see
  Item 33's entry above for the exact numbers). Two items in this phase
  (29, 31) surfaced the same underlying lesson worth carrying into Phase
  4: several remaining "reusable pattern" hypotheses in this codebase are
  narrower in practice than they look on paper, because dcc's MIR
  construction already does local value numbering and never CSEs source
  constants - future candidates in this vein should be census-checked
  for real corpus hits *before* investing in correctness testing and
  emitter wiring, not after. Phase 3 is complete; Phase 4 (Items 35-44)
  has not yet been started.

- **Item 35** (2026-08-01): added `mir_thread_jumps()` to
  `src/dcc/dcc_mir.c`, called from `mir_end_function()` immediately
  before `mir_resolve_deferred_metadata()` (so it runs before any
  selector/CFG-successor/PHI-construction pass observes the CFG). For
  every `MIR_JUMP`/`MIR_BRANCH_FALSE`, if the target label is immediately
  followed by an unconditional `MIR_JUMP` to a different target, the
  original instruction's `.label` is retargeted straight to that final
  destination (single-level chase only; a `target == insn->label`
  self-jump is left alone to avoid a pointless no-op assignment). This is
  pure jump threading: the intermediate label was always going to fall
  straight through to that same jump, so no input can observe a
  difference in which instruction executes next.

  Investigated the motivating case study (`and_expr` in `tests/adaint.c`,
  `while (acc_word("and")) { ... }`) directly via `DCC_MIR_REPORT=1`: its
  `continue_label` (`L3`) turned out to have *no* incoming jump at all in
  this specific function (the loop body has no `continue;` statement, so
  `L3` is reached only by fallthrough from the previous statement) -
  meaning this exact function doesn't exercise the new pass at all; the
  real fix for its redundant `L3: jp L1` structure is Item 37's dead-label
  removal, not jump threading. Constructed a second, more representative
  synthetic case instead (`while` loop with an explicit `continue;`
  statement, `/tmp/titem35b.c`, not committed) and confirmed via
  `DCC_MIR_REPORT=1` that the `continue;` statement's jump - previously
  emitted as `jump L3` where `L3: jump L1` - now emits directly as
  `jump L1`, with `L3` left as a dead, unreferenced label (Item 37's
  future job to remove). This is the real target population: `while`/
  `do-while` loops whose `continue_label` has no loop-increment work,
  reached via an explicit `continue;` rather than only by fallthrough.
  Verified correctness by building and running this synthetic case under
  `ntvcm` (four `n` values including `n=0` and `n=1` boundary cases): all
  matched hand-computed expected sums. Confirmed by inspecting the
  `AST_WHILE`/`AST_FOR`/`AST_DOWHILE` lowering that `for` loops don't
  exhibit this exact shape as often, because their `continue_label` is
  where the increment expression lives, not an empty jump-only block -
  so the highest-yield population for this fix is specifically
  `while`/`do-while` loops with a `continue;` statement in the body.

  Full census vs `build/phase3-after.tsv`: 0 regressions, 0 newly
  MIR-emitted, 0 no-longer-emitted, coverage unchanged at 172/2371
  (7.25%, expected - this pass only reshapes jump targets, it doesn't
  change any acceptance decision by itself; that's Item 40's job after
  Items 35-39 land together). Only 2 apps showed any census metric
  change at all (`tc89swjt`, `too`), both fallback-only functions whose
  byte/instruction counts shifted from the jump retargeting; census
  reported 0 apps requiring runtime validation. Focused `-Mode full` run
  on those 2 apps plus `adaint` and several loop/continue-heavy apps
  (`tdowhile`, `tforcomm`, `tforinc`, `tforpred`, `tlcont`, `tnestfor`):
  all passed, 0 regressions. Milestone `-Mode full -Extended`: 313/322
  apps passed (9 skipped as expected), 196/196 extended passed,
  diagnostics/dccpeep fixtures/performance all clean.

- **Item 36** (2026-08-01): generalized `mir_thread_jumps()` to chase
  transitive jump-to-jump chains (`label -> jump -> label -> jump ->
  ... -> final target`) instead of only a single hop, tracking each
  visited label id in a small fixed-size buffer (`MIR_THREAD_JUMPS_MAX_
  CHAIN`, 256) so a pathological cycle - which real lowering cannot
  produce, since each jump-only link has exactly one successor and the
  label graph is otherwise acyclic, but which would spin the chase
  forever if it somehow existed - stops the chase at the last good
  target instead. Also fixed the chase to skip over `MIR_NOP`
  instructions between a label and the jump that follows it: user-named
  `goto` labels get an `MIR_NOP` carrying the source label's name
  immediately after the `MIR_LABEL` for diagnostics (compiler-
  synthesized loop labels like `continue_label` don't), which hid an
  otherwise-identical jump-only shape from Item 35's original
  immediately-next-instruction check. Confirmed via `dcc_mir.c`'s
  emitter that `MIR_NOP` never emits any code (`case MIR_NOP: break;`),
  so skipping past one changes nothing about which instruction the
  retargeted jump actually reaches.

  Verified directly with a synthetic four-hop `goto` chain
  (`a: goto b; b: goto c; c: goto d; d: return n*2;` guarded by an
  `if (n > 0)`, not committed): `DCC_MIR_REPORT=1` confirmed the
  original `goto a`'s jump, which previously chased only as far as the
  next link, now retargets directly to the final block with real work,
  collapsing all four intermediate jump-only labels in one pass; ran
  under `ntvcm` and got the correct results for both the positive and
  non-positive branches.

  Full census vs `build/phase3-after.tsv`: 0 regressions, coverage
  unchanged at 172/2371 (7.25%, expected for the same reason as Item 35
  - this pass reshapes jumps, not acceptance decisions). 7 apps showed
  census metric changes (`a1`, `bint`, `cpmenumd`, `forint`, `tc89swjt`,
  `tchess`, `too` - the nop-skip fix widened the pattern's reach to
  `goto`-heavy code), 0 apps required runtime validation per the census.
  Focused `-Mode full` on those 7 plus `adaint`/`tgoto`/`tgotocap`: all
  passed, 0 regressions, and one genuine improvement surfaced -
  `tgoto` dropped 45,022->44,838 cycles (-0.41%) and 6,272->6,144 bytes
  (-2.04%) in peep mode, with matching nopeep improvements - accepted via
  `-UpdatePerfBaseline` (purely additive single-row change, verified with
  `git diff`). Milestone `-Mode full -Extended`: 313/322 apps passed (9
  skipped as expected), 196/196 extended passed, diagnostics/dccpeep
  fixtures/performance all clean.

- **Item 37** (2026-08-01): **deferred - Item 6-level design ambiguity,
  same caution bar.** Implemented `mir_remove_dead_labels()` (neutralize
  any `MIR_LABEL` to `MIR_NOP` when its id is referenced by neither a
  live `MIR_JUMP`/`MIR_BRANCH_FALSE` target nor any existing `MIR_PHI`'s
  `phi_pred1`/`phi_pred2`), first alongside `mir_thread_jumps()` and then,
  after discovering a problem, moved to run after `mir_verify_and_dump()`
  so it wouldn't preempt that pass's own `mir_promote_objects()` call.
  Neither placement is actually safe, and the reason is architectural,
  not a simple ordering bug: `mir_try_make_object_phi()` (the loop-header
  object-phi promotion this item was explicitly supposed to build on per
  Item 38's note) identifies a candidate phi's predecessor blocks via
  `mir_block_label_before()`, which walks *backward from an instruction's
  physical position* to the nearest preceding `MIR_LABEL` - it has
  nothing to do with whether that label is still a live jump target. A
  label with zero incoming jumps can still be the sole physical identity
  of a block whose own outgoing edge (most commonly a loop latch's
  unconditional jump back to the header, i.e. exactly the shape Items
  35/36 just orphaned) feeds a real, still-relevant predecessor edge.

  Caught this empirically, not just by re-reading the code: a synthetic
  `while` loop with `continue;` (`sum_evens_while`, the same shape used
  to validate Item 35/36) has a genuine loop-header object phi merging
  the entry value of `i` and its loop-latch value - confirmed present in
  the `DCC_MIR_REPORT=1` dump before this item's code existed. After
  neutralizing the orphaned entry label and/or the orphaned continue
  label, the phi silently stopped forming (the loop fell back to a plain
  per-iteration reload of `i` instead) - `DCC_MIR_FORCE_ACCEPT_FUNCTION`
  confirmed the function still emits *correct* code either way (this is
  a missed-optimization regression, not a miscompile, matching the
  general safety argument from Item 35/36's design phase), but it is a
  real, measurable step backward for exactly the promotion Items 38/39
  are supposed to extend.

  A correct fix needs to protect any label that is `mir_block_label_
  before()` of *any* instruction whose successor is a real `MIR_LABEL`
  block start, not just labels currently referenced by a live jump or an
  already-existing phi. Tried this stricter rule by hand-tracing it
  against both test cases: it correctly preserves `sum_evens_while`'s
  phi, but it also ends up protecting `and_expr`'s orphaned labels too
  (its continue-label's block still has a real outgoing edge to the loop
  header, even though `and_expr` has no loop-carried scalar to ever phi-
  promote there) - meaning the stricter, actually-safe rule protects
  almost every label in any function with a loop or branch, leaving
  little to no real reduction in `mir_cfg_block_count()` and defeating
  this item's motivating purpose. A version that reclaims the block-count
  win without this cost would need real relabeling/redirection plumbing
  (propagating a removed block's identity forward into any phi that
  keyed on it) well beyond this item's "smallest reusable edit" scope.

  Reverted all Item 37 code; `src/dcc/dcc_mir.c` is back to exactly its
  post-Item-36 committed state (confirmed via `git diff` showing no
  changes before rebuilding). Deferring rather than shipping an unsound
  or vacuous version - if `mir_cfg_block_count()` needs to look past
  Items 35/36's newly-orphaned labels, the eventual fix belongs together
  with the CFG-successor/predecessor-tracking rework Items 38/39 already
  need for the same underlying data, not as an isolated label-removal
  pass. Moving on to Item 38.

- **Item 38** (2026-08-02): Investigated `mir_promote_objects()`/
  `mir_try_make_object_phi()` (`dcc_mir.c` ~4143-4356) before writing any
  code, because Item 37 had just shown this machinery is easy to
  misjudge. Found it is *already* a fully general dataflow pass, not
  loop-specific: it walks every instruction's reaching object state and
  calls `mir_try_make_object_phi()` whenever a load or object-merge sees
  `MIR_OBJECT_AMBIGUOUS` (predecessors disagree), and directly aliases to
  the existing value with no phi at all when predecessors already agree
  (`reaching >= 0`) - this second case is exactly non-loop straight-line
  reuse and evidently already works. The real gap was structural, not
  semantic: `mir_try_make_object_phi()` requires
  `mir_block_label_before(predecessor) >= 0` for *every* predecessor, and
  a plain `if (cond) { ... } else { ... }` statement's then-arm is pure
  fallthrough right after the `MIR_BRANCH_FALSE` - it has no label of its
  own (nothing ever jumps to it), so `mir_block_label_before()` walks
  straight past it to whatever precedes the `if`, or hits the `brfalse`
  itself and returns -1. Confirmed this by hand with a synthetic function
  (`pick`, `if (cond) y = x*2; else y = x*2;` then `return y + x;`):
  `DCC_MIR_OBJECT_REPORT=1` showed `y`'s post-join load stuck at
  `state=-2` (ambiguous) with the phi attempt silently failing every
  time, even though both arms compute the identical expression.

  Fix: in `mir_lower_stmt()`'s `AST_IF` case, emit a label at the start
  of the then-arm - giving it the same physical predecessor identity the
  else-arm/end-label already have - but *only* when `node->c != NULL`
  (there is an else-arm, so a genuine two-predecessor join exists at
  `end_label`). First tried this unconditionally for every `if`
  (including bare `if` with no `else`) and caught a real regression via
  the regression-gated census before committing anything: `tlongopt`'s
  `br_lt_hex_u`/`br_ult_hex_u` (`if (x < 0xffff) return 1; return 0;`,
  no else, nothing to ever merge) flipped from accepted to fallback,
  because the unconditional extra label perturbed the narrow
  whole-function `homed-scalar-cfg` selector into materializing an extra
  block boundary - `generated-bytes` rose 191->197 even though nothing
  needed the label. Scoping the label to the `node->c != NULL` case only
  fixed both regressions with no loss to the intended non-loop-join
  case (verified `pick` still produces `phi v15 = v9,v13 y {o4} type=2
  [L3,L1]` at the join, directly merging the two live values instead of
  reloading `y` from memory).

  Took a real before/after census pair (`build/item38-before.tsv` built
  from the pre-Item-38 binary, since `phase3-after.tsv` predates Items
  35/36): first pass (unconditional label) failed
  `--fail-on-regression` with 2 functions regressed
  (`tlongopt.br_lt_hex_u`, `tlongopt.br_ult_hex_u`); after scoping the
  fix to the `else`-present case, the census passed clean - 0
  regressions, coverage 172/2371 -> 173/2371 (+1,
  `tc99scpe.conditional_decl`), 72 apps with census metric changes
  (mostly fallback-only label-count churn), 3 apps needing runtime
  validation (`cint`, `tc99scpe`, `tdead`). Focused `-Mode full` on those
  3 plus `tifcom`, `tlongopt`, `tphi`, `tmircfg`, `tinlnpar` (if/else and
  phi-heavy apps): all 8 passed, 0 regressions, 9 genuine cycle/byte
  improvements (`tmircfg` -8.66% nopeep cycles/-2.38% bytes, `tdead`
  -0.66%, `tc99scpe` -0.99%, `tlongopt` -0.05%, all with matching peep
  numbers) - accepted via `-UpdatePerfBaseline` (verified a clean 4-row
  diff, all decreases, no byte increases). Milestone `-Mode full
  -Extended`: 313/322 apps, 196/196 extended, diagnostics/dccpeep/
  performance all clean.

- **Item 39** (2026-08-02): **deferred - real static win, but a
  measured runtime regression a byte/instruction-count gate can't
  safely catch.** Extended Item 38's then-arm labeling to the bare
  `if (cond) { ... }` case with no `else` - the then-arm still falls
  straight through into `else_label` (which is already the join point
  there), so this is structurally the same missing-predecessor-identity
  gap Item 38 fixed for if/else, just with the fallthrough arm playing
  the role of the second predecessor instead of a real else-arm.
  Confirmed empirically first: a synthetic `bump` (`int y = a; if (cond)
  y = a + 1; return y * 2;`) showed the same `state=-2` stuck-ambiguous
  symptom Item 38 found, for the same `mir_block_label_before()` reason.

  First tried labeling every bare `if` unconditionally. This reproduced
  Item 38's exact `tlongopt.br_lt_hex_u`/`br_ult_hex_u` regression
  (`if (x < 0xffff) return 1; return 0;`, which never reaches any join
  at all): 191 -> 197 bytes, fallback. Suspected `mir_cfg_block_count()`
  itself was the selector's real gate (it backs several exact-match
  whole-function shape checks, e.g. `== 7`, `== 4`, and cost heuristics
  like `<= 2 ? 2 : 1`), so tried making it "live-label-aware" - only
  counting labels with a real incoming jump or phi-predecessor
  reference, ignoring dead ones. This was a bigger mistake than it
  looked: every function's own entry label is *always* "dead" by this
  definition (nothing inside a function ever jumps back to its own
  entry), so this change silently dropped the block count of every
  straight-line function by 1, breaking the `== 1`/single-basic-block
  cost-exception checks used throughout the selector set. Census caught
  it immediately: 13 previously-accepted functions regressed to
  fallback, a net -9 coverage swing versus Item 38's state. Reverted
  `mir_cfg_block_count()` back to its original plain definition and
  confirmed via census that this alone was not the fix - `tlongopt`
  still regressed with the label always present. Root cause was
  narrower and more architectural than a block-count gate: the
  register-homing emitter itself conservatively treats crossing *any*
  `MIR_LABEL` (dead or live) as an unknown-register-state boundary
  requiring extra reload/preserve overhead, independent of whether
  anything actually jumps there.

  Added a narrow, false-favoring `mir_stmt_always_returns()` syntactic
  check (recognizes `AST_RETURN` directly, an `AST_COMPOUND` ending in
  one, and `AST_IF`/`else` where both branches always return; anything
  else - loops, switch, goto, break/continue - is conservatively treated
  as "may fall through", so this only ever wastes an unused label, never
  skips a needed one) and gated the then-arm label on
  `!mir_stmt_always_returns(node->b)`. This fixed `tlongopt`: both
  `bump`'s phi now formed correctly, and `br_lt_hex_u`/`br_ult_hex_u`
  were back to 191 bytes/accepted, since the then-arm there always
  returns and the label got skipped.

  Ran the full regression-gated census: clean, 0 regressions, coverage
  173/2371 -> 175/2371 (+2: `tc99scpe.conditional_decl`,
  `tc99scpe.if_body_decl`, `tinlnpar.clamp_low`), 216 apps with census
  changes, 12 apps needing runtime validation. Focused `-Mode full` on
  those 12 plus `tifcom`, `tlongopt`, `tphi`, `tmircfg`, `tdead`: all
  17/17 passed correctness, but 2 performance regressions surfaced -
  `tinlnpar` +0.53% peep cycles / +0.52% nopeep cycles (19128 -> 19229,
  19372 -> 19473). Root-caused to `tinlnpar.clamp_low` (`if (cond)
  idx = 0; return idx + 1;` - exactly this item's target shape), newly
  MIR-accepted by this fix. Confirmed via forced A/B
  (`DCC_MIR_FORCE_ACCEPT_FUNCTION=clamp_low` vs
  `DCC_MIR_FORCE_FALLBACK_FUNCTION=clamp_low`) that the MIR version is
  genuinely slower at runtime, despite having fewer static bytes/
  instructions than the legacy version it replaced - a direct instance
  of SKILL rule #4 ("a smaller assembly-text stream or instruction count
  is not proof of faster or smaller Z80 code").

  Hand-disassembled both versions and computed Z80 T-state costs for
  both branch outcomes: the two approaches are asymmetric, not
  uniformly better or worse - MIR is faster on the `cond` **true**
  (`idx = 0`) path (166 vs legacy 204 T-states) but slower on the
  **false** (`idx` unchanged) path (187 vs legacy 149 T-states). For
  `tinlnpar`'s exact call pattern (`clamp_low(1,5)` then
  `clamp_low(0,5)`, one call of each outcome), the hand-computed totals
  came out exactly equal (353 T-states either way) - yet the measured
  runtime still showed a real, reproducible regression, meaning either
  an uncounted cost exists (e.g. prologue/epilogue/frame differences
  between the two emitted shapes) or some other subtlety wasn't
  captured by the by-hand model. Either way, this is a genuine
  cost-modeling gap: a single aggregate byte/instruction-count gate
  cannot safely predict runtime cost for a phi-join shape whose two
  incoming branches have asymmetric costs, and no cheap structural
  predicate found here closes that gap.

  Reverted this item's code back to exactly Item 38's committed shape
  (label scoped to `node->c != NULL` only; removed the now-unused
  `mir_stmt_always_returns()` helper) - confirmed via `git diff` that
  `src/dcc/dcc_mir.c` matches the `d654327` commit exactly, and via a
  fresh census against `build/item38-before.tsv` that behavior is back
  to Item 38's exact post-state (173/2371 coverage, same 72-app delta).
  Deferring rather than shipping a measured regression, in the same
  spirit as Items 6/37: a real fix for the bare-if/fallthrough join case
  needs either a genuine cycle-cost model for phi-containing candidates
  or per-branch-outcome profiling, not a single static byte/instruction
  gate - that is future work beyond this item's "smallest reusable
  edit" scope. Moving on to Item 40.

- **Item 40** (2026-08-02): Mandatory re-scoping checkpoint before
  Phase 5. Ran a fresh full census and compared it against
  `build/phase3-after.tsv` (the pre-Item-35 baseline, i.e. the actual
  starting point of the set of items that landed: 35, 36, 38 - Item 37
  and 39 were both deferred with no code change surviving). Result:
  `cfg-block-count` fallback still has exactly the same 3 functions
  (`adaint.run`, `cobint.exec_range`, `fint.prim`), and `cfg-backedge`
  still has exactly the same 2 (`tcrcfix.bcd_div10`, `tregnarw.lres`) -
  none moved to accepted. `cobint.exec_range`'s block count rose
  slightly (130 -> 132) and `fint.prim`'s did too (94 -> 95), most
  likely from Item 38's then-arm label adding one block on some
  internal if/else inside those functions; this had no effect on their
  fallback status since they were already well past any of the
  relevant exact-match/threshold gates in both directions. Overall
  outcome counts: `text-size` 2141 -> 2140, `accepted` 172 -> 173 (the
  net Item 38 gain), everything else unchanged. Conclusion: Items
  35/36/38's jump-chain collapsing and phi-join labeling are real but
  too localized to shift any of these 5 functions' raw label/block
  counts across the specific thresholds their selectors gate on -
  Phase 5's loop-competitiveness work should not assume any "for free"
  wins here from Phase 4 alone. No code change; scratch census files
  removed per SKILL rule 7. Moving on to Item 41.

- **Item 41** (2026-08-02): Added `DCC_MIR_PHI_REPORT=1` to
  `mir_try_make_object_phi()` (`dcc_mir.c` ~4159): every early-return
  path now reports a miss with a specific reason
  (`unlabeled-block`, `too-many-predecessors`,
  `predecessor-unresolved`, `insufficient-predecessors`,
  `already-identical`), and the success path reports a hit with the
  destination value and both predecessor values/labels. This gives
  direct visibility into exactly which join case Items 38/39
  investigated by hand (`unlabeled-block`/`predecessor-unresolved` was
  the Item 39 bare-if/fallthrough symptom) without needing a one-off
  synthetic test and manual object-report reading each time. Verified
  against the same synthetic functions used in Items 38/39: `pick`
  (if/else, identical value both arms) reports `phi hit ... pred1=v5@L3
  pred2=v9@L1`; `bump` (bare if, no else) reports `phi miss ...
  reason=predecessor-unresolved ... label=-1`, confirming the guard
  correctly identifies the still-unresolved fallthrough case Item 39
  deferred. Diagnostic-only change (no behavior change): regression-
  gated census vs `build/phase3-after.tsv` is byte-for-byte identical
  to Item 40's snapshot (173/2371 coverage, same 5 remaining
  cfg-block-count/cfg-backedge functions). Focused `-Mode full` on
  `cint, tc99scpe, tdead, tphi`: 4/4 passed, 0 regressions. Milestone
  `-Mode full -Extended`: 313/322 apps, 196/196 extended, diagnostics/
  dccpeep/performance all clean. Moving on to Item 42.

- **Item 42** (2026-08-02): Added `tests/tphijoin.c` (named for CP/M 8.3's
  8-char base limit; `tmircfgshape` is too long), a permanent regression
  fixture covering the CFG-shape hygiene surface Items 35-41 touch:
  `if_else_join` (Item 38's target - both arms of an if/else define the
  same value identically), `nested_if_else_join` (an inner if/else join
  feeding an outer if/else join, stacking Item 38's fix two levels deep -
  confirmed via `DCC_MIR_PHI_REPORT=1` this currently still misses with
  `reason=too-many-predecessors`, since the outer join sees 3 incoming
  edges once the inner join is counted, an open gap beyond this item's
  scope), `sum_even_with_continue` (Items 35/36's jump-chain-collapsing
  target - a `while` loop with an interior `continue`, confirmed via
  `DCC_MIR_SELECT_REPORT=1` still falls back on `text-size` for this
  particular function, but exercises the collapsing machinery), and
  `loop_header_phi` (the loop-carried value phi Item 37 found already
  works, confirmed still forming via two `phi hit` reports for `total`
  and `i`). All expected values were cross-checked against a native
  `clang` build of the source (`clang -Wall tests/tphijoin.c`, ignoring
  the expected K&R-prototype warnings) before wiring the dcc baseline,
  matching the Item 21/32 precedent. Added
  `tests/baselines/tphijoin.txt` and captured initial peep/nopeep perf
  baselines via `-UpdatePerfBaseline` (clean single-row first-time
  capture, not a movement of an existing row). Validated: `runall
  -Apps tphijoin -Mode full` (fast+nopeep, both pass, stack-check
  enabled), then milestone `-Mode full -Extended`: 314/323 apps passed
  (9 skipped as expected, +1 app vs Item 41's count for the new
  fixture), 196/196 extended, diagnostics/dccpeep/performance all
  clean. Moving on to Item 43.

- **Item 43** (2026-08-02): Full census vs `build/phase3-before.tsv`
  (the true pre-Phase-4 baseline, predating Items 25-42): **0
  regressions**, coverage 165/2319 (7.12%) -> 173/2378 (7.28%, +8
  functions vs the phase's start; 7 of the 2378 total are new functions
  from Item 42's `tphijoin.c` fixture itself), 2 newly MIR-emitted
  (`tc99scpe.conditional_decl`, `tmirfast.side_effect`), 0 no-longer-
  emitted, 167 apps with census metric changes, 9 apps flagged for
  runtime validation. Focused `-Mode full` on all 9
  (`cint, tc99scpe, tdead, tesc, tmirfast, tscanf, tsprintf, tstr3,
  tsyntax`): 9/9 passed, 0 regressions.

- **Item 44** (2026-08-02): Phase 4 milestone checkpoint. **Phase 4
  summary (Items 35-44):** coverage moved 172/2371 (7.25%) -> 173/2378
  (7.28%) across the phase (net +1 MIR-accepted function, plus 7 new
  functions from the phase's own regression fixture). Landed: Item 35
  (jump-to-jump collapse via `mir_thread_jumps()`, `ca4cf6b`), Item 36
  (transitive jump-chain generalization, bounded iteration, `d0bab33`),
  Item 38 (if/else then-arm labeling enabling object-phi reuse across
  two-predecessor joins, `d654327`), Item 40 (re-scoping census
  confirming Items 35/36/38 did not shift any
  `cfg-block-count`/`cfg-backedge` fallback to accepted), Item 41
  (`DCC_MIR_PHI_REPORT=1` diagnostic), Item 42 (permanent regression
  fixture `tests/tphijoin.c`), and this item's full-corpus close-out.
  Deferred with documented rationale (same Item-6-level caution bar):
  Item 37 (removing zero-predecessor labels after Items 35/36 - breaks
  object-phi promotion's block-identity lookup for still-relevant
  loop-latch labels with zero *incoming-jump* references but a real
  outgoing edge) and Item 39 (extending Item 38's fix to a bare-if/no-
  else fallthrough join - fixed the static-byte regression it first
  reintroduced from Item 38's history, but then exposed a genuine,
  asymmetric-branch-cost runtime regression, `tinlnpar.clamp_low`, that
  a static byte/instruction gate cannot safely catch). The dominant
  theme this phase, echoed across Items 37/38/39: `mir_promote_objects()`
  /`mir_try_make_object_phi()` is a real, general dataflow pass whose
  only practical blocker is *label-based predecessor identity*, not
  semantic ambiguity - every remaining gap in this area is really "does
  this block have a stable, physical label" rather than "can the
  compiler prove these values agree." Milestone `-Mode full -Extended`:
  314/323 apps passed (9 skipped as expected), 196/196 extended,
  diagnostics/dccpeep/performance all clean. Phase 4 is complete; Phase
  5 (Items 45-56, loop competitiveness generalization - flagged in the
  plan as needing the most caution) has not yet been started.


- **Item 45** (2026-08-02): Mandatory re-scoping census before Phase 5.
  Fresh census vs `build/phase3-before.tsv`: 3 functions still fail on
  `cfg-backedge` - `tcrcfix.bcd_div10`, `tphijoin.loop_header_phi` (new,
  from our own Item 42 fixture), `tregnarw.lres`. Confirmed Items
  35/36/38 did not shift any of these (same set as Item 40's earlier
  snapshot, plus the new fixture function). **Major discovery, made by
  reading `mir_end_function()`'s dispatch cascade carefully**: the 4
  specialized loop selectors (`mir_try_emit_countdown_loop`,
  `mir_try_emit_accumulator_loop`, `mir_try_emit_unsigned_division_loop`,
  `mir_try_emit_repeated_invariant_add_loop`) are called *only* from
  `mir_try_emit_z80()`, which is itself reachable *only* via the
  diagnostic `DCC_MIR_CANDIDATES`/`DCC_MIR_EMIT_FUNCTION` env-var
  overrides - never from production's default dispatch path. Verified
  three independent ways: (a) code reading of the cascade in
  `mir_end_function`, showing the production `else` branch only ever
  tries `homed-scalar-cfg` then `spilled-scalar-cfg`; (b) empirically,
  `DCC_MIR_EMIT_FUNCTION=bcd_div10` still fell back identically, because
  `mir_try_emit_z80()`'s own internal ordering tries `homed-scalar-cfg`
  FIRST and that succeeds structurally before ever reaching the loop
  selectors; (c) a census-wide selector-name distribution across all
  2378 functions showing zero occurrences of any loop-family selector
  name (only `homed-scalar-cfg`/`spilled-scalar-cfg`/`selector`
  appeared). This reframes Phase 5 entirely: Items 46-48 as originally
  scoped (generalizing the loop selectors' structural coverage) would
  have had zero production effect until the dispatch-wiring gap itself
  is fixed - the selectors were fully correct but unreachable. Folded
  into Item 46's fix below rather than committed standalone.

- **Item 46** (2026-08-02): Reframed from "generalize
  `mir_try_emit_countdown_loop`" to "fix the dispatch-wiring gap Item 45
  found" - generalizing dead code first would have been wasted work.
  **Fix**: in `mir_end_function()`, when the fallback-reason cascade
  computes exactly `"cfg-backedge"` (meaning every earlier cost gate -
  `text-size`, `instruction-count`, `cfg-block-count`,
  `inline-substitution`, `pointer-array` - already passed for the
  homed/spilled candidate) and the function returns `TYPE_INT`, retry
  the 4 specialized loop selectors into a fresh stream, in
  `mir_try_emit_z80`'s exact priority order (accumulator ->
  unsigned-division -> repeated-invariant-add -> countdown). If one
  structurally matches, apply the same near-cost/byte-profitability
  checks (explicitly excluding the `cfg-backedge` veto itself, since
  matching one of these hand-verified structural selectors is the proof
  of safety for that specific loop shape) and swap it in as the accepted
  candidate. This can never affect any function homed/spilled-scalar-cfg
  would otherwise accept outright - it only activates when the *only*
  remaining objection is the generic backedge veto and the alternative
  is legacy fallback anyway. **Result**: `tcrcfix.bcd_div10` (repeated-
  subtraction unsigned division loop) now matches
  `mir_try_emit_unsigned_division_loop` exactly with no generalization
  needed - the selector was already correct, only unreachable. `lres`
  still falls back as predicted: it takes no parameters at all, so none
  of the 4 selectors' `SC_PARAM`-storage requirement for the loop
  counter is met (a separate, deferred generalization - accepting
  `SC_LOCAL`-storage loop variables whose only reaching value is a
  constant at function entry). `loop_header_phi` (our own `tphijoin.c`
  fixture, `for(i=1;i<=n;i=i+1) total+=i;`) also still falls back as
  expected: `n` is only a comparison bound, never itself the decrementing
  counter - none of the 4 selectors target an ascending-counter-vs-
  parameter-bound shape (they all require the parameter itself to BE the
  counter, counting toward zero). Both are distinct, larger structural
  gaps left for later items, not this one's scope. **Validation**:
  regression-gated census vs `build/phase3-before.tsv`: **0
  regressions**, coverage 173/2378 -> 174/2378 (+1: `tcrcfix.bcd_div10`),
  only `tcrcfix` app affected. Focused `-Mode full` on `tcrcfix`: passed,
  with real measured cycle improvements (not just static bytes):
  -5.75% peep cycles (234962 -> 221446), -7.95% nopeep cycles (268864 ->
  247494), -1.22% peep bytes (10496 -> 10368), nopeep bytes unchanged
  (11136). A forced-fallback A/B sanity check
  (`DCC_MIR_FORCE_FALLBACK_FUNCTION=bcd_div10`) also passed cleanly,
  confirming correctness either way. Accepted the verified improvement
  via `-UpdatePerfBaseline` (clean single-row movement, all metrics
  improved, no regressions). Milestone `-Mode full -Extended`: 314/323
  apps passed (9 skipped as expected), 196/196 extended, diagnostics/
  dccpeep/performance all clean. This is the first time any of the 4
  specialized loop selectors have ever produced live production Z80
  code. Moving on to Item 47, which (per Item 45's discovery) should be
  re-evaluated for real production reachability now that the dispatch
  gap is fixed, rather than assumed still-relevant as originally scoped.

- **Items 47-56** (2026-08-02): Phase 5 close-out. After Item 46's
  dispatch-wiring fix, the only remaining `cfg-backedge` functions are
  `tregnarw.lres` and `tphijoin.loop_header_phi` - re-checked every
  originally-scoped generalization item (47/48/50/51/52) against this
  concrete, exhausted population before writing any code, per the
  skill's evidence-based prioritization rule ("a lower-yield repeated
  fix is preferable to a broad gate relaxation with no cost model").
  None had a real target:
  - **Item 47** (multi-accumulator): no function in the corpus needs
    more than one accumulator in this loop shape; deferred.
  - **Item 48** (signed division loop): `bcd_div10` was the only
    division-loop candidate and it's already unsigned and now
    accepted (Item 46); deferred.
  - **Item 49** (materialize-then-retest bug check): completed by
    inspection - all 4 specialized loop selectors
    (`mir_try_emit_countdown_loop`, `mir_try_emit_accumulator_loop`,
    `mir_try_emit_unsigned_division_loop`,
    `mir_try_emit_repeated_invariant_add_loop`) emit hand-crafted Z80
    directly from arithmetic flag state (`jp nc`/`jp z`/`jp m`
    immediately after the controlling operation) and never call
    `mir_emit_scalar_compare` or any shared comparison-materialization
    path, so the Phase 1 bug class (explicit 0/1 boolean materialized
    to a slot, then reloaded and re-tested) cannot occur in this code.
    No fix needed; documented as a verified negative result.
  - **Item 50** (spilled-case invariant hoisting): checked the fresh
    census for any `spilled-scalar-cfg` function that is both accepted
    (`result=mir`) and loop-shaped (backedge) - found none; the only
    multi-block accepted `spilled-scalar-cfg` function
    (`cint.if_stmt`) is a branch chain, not a loop. `729bc11`'s
    `mir_promote_objects()` fix already runs unconditionally before
    selector dispatch (line ~4776), so it isn't actually gated by
    "homed vs. spilled" at all - the real distinction the item's title
    assumed doesn't exist in the current architecture. Combined with
    the SKILL.md's documented finding that spilled-path functions are
    systemically ~2x more expensive (not marginal), a narrow
    invariant-hoisting improvement is very unlikely to flip any
    current fallback to accepted. Deferred.
  - **Item 51** (VLA power-of-two loop re-profile): explicitly depends
    on Item 52's "now-cheaper general loop path," which itself has no
    evidence-backed target (see below). Deferred alongside Item 52.
  - **Item 52** (general arbitrary-loop-body selector): investigated
    `tregnarw.lres` and `tphijoin.loop_header_phi` in MIR detail via
    `DCC_MIR_REPORT=1`/`DCC_MIR_FUNCTION`. Neither is a case of "the
    existing narrow selectors would match if only the storage-class
    check were relaxed": `lres`'s `while (--n) total += n;` decrements
    *before* both the compare and the accumulate (using the
    post-decrement value for the add), the opposite instruction order
    from `mir_try_emit_accumulator_loop`'s `while (n>0) { sum+=n;
    --n; }` pattern (which adds the pre-decrement value, then
    decrements) - a genuinely different dataflow shape, not just a
    storage-class gap. `loop_header_phi`'s ascending counter compared
    against a parameter bound is likewise a distinct shape none of the
    4 selectors target. Building a brand-new fully general selector
    speculatively, with no concrete failing corpus function that it
    would specifically unlock beyond these two structurally-unrelated
    single-function cases, is exactly the kind of broad,
    evidence-free generalization the skill warns against - this is an
    Item-6-level design ambiguity (large scope, "highest-risk item in
    the plan," mandatory forced-A/B-profiling per Item 53, but zero
    real corpus pressure to justify the risk right now). Deferred with
    this rationale; Items 53-56 (profiling/tests/validation for Item
    52's selector) are deferred alongside it, contingent on future
    evidence (e.g., a new test fixture or discovered app deliberately
    exercising this shape) rather than built speculatively now.
  No code changes were required for this batch (all either verified
  clean or deferred); no rebuild/census/full-mode run was needed since
  `dcc_mir.c` is unchanged from Item 46's committed state. Phase 5
  (Items 45-56) is closed: the one real, evidence-backed win was
  Item 46's dispatch-wiring fix (`bcd_div10`, +1 coverage). Moving on
  to Phase 6 (Items 57-66, cost model & inline-substitution admission).

- **Items 57-66** (2026-08-02): Phase 6 close-out. Item 57's fresh
  census confirms 24 `inline-substitution` fallback functions,
  unchanged from before Phase 1-5 (`cobint` x17: `compile_add`,
  `compile_compute`, `compile_condition`, `compile_divide`,
  `compile_expr`, `compile_factor`, `compile_goto`, `compile_move`,
  `compile_multiply`, `compile_perform`, `compile_primary`,
  `compile_rel_one`, `compile_store_after_value`, `compile_subtract`,
  `compile_term`, `compile_var_load`, `need`, `run_bc`; `forint` x2:
  `assign_pre`, `bump_sym_val`; `tinline` x3: `inline_nest_check`,
  `inline_order_check`, `nest_scale_and_clamp`; `tinlinfb` x1: `main`).
  Traced the gate's real meaning by code reading: `dcc_ast_gen_expr.c`
  already implements a full AST-level `static inline` call-site body
  substitution (`inline_substitution_body`,
  `try_gen_inline_call_ast`, `clone_inline_expr`, and supporting
  argument-temp machinery, ~300 lines around line 3765-4148) that
  legacy's capture uses whenever a caller invokes a `static inline`
  function - producing the "captured" byte/instruction counts these
  24 functions are compared against. MIR's own lowering
  (`mir_lower_expr`'s call-emission path, `dcc_mir.c` ~2038) never
  attempts substitution at all: it unconditionally emits a plain
  `MIR_CALL` for every callee, merely tagging calls to substitutable
  functions with a diagnostic flag (`memory_flags |= 2048`) that
  `mir_has_inline_substitution_call()` (~10120) uses purely to trigger
  the cautious size-parity veto in `mir_end_function()`'s cascade.
  **This confirms Items 58-66 are not a gate-relaxation or emitter
  tuning question at all - they describe building an entirely new
  MIR-native call-site inlining pass from scratch**, duplicating (at
  the MIR IR level) machinery the AST backend already has at the
  source level. This is Item-6/Item-52-level design ambiguity, for
  three concrete reasons: (1) the discriminator column for Item 58
  itself documents that "the previously-tried unconditional expansion
  enlarged nested bodies and didn't admit callers" - a prior attempt
  at this exact feature is on record as having already failed; (2)
  today's fallback for all 24 functions is fully correct (byte-
  identical legacy replay per the mixed-mode architecture), so there is
  no correctness debt whatsoever, only a coverage/purity metric; (3)
  even a fully successful implementation caps out at 24 of 2378
  functions (~1% of the corpus), a modest yield for a high-effort,
  previously-failed, novel compiler pass with its own mandatory cost
  model, multi-call-site extension, and per-candidate forced-profiling
  gate (Item 62). Per the skill's evidence-based prioritization ("a
  lower-yield repeated fix is preferable to a broad gate relaxation
  with no cost model") and the standing guidance to defer genuine
  Item-6-level ambiguity rather than attempt a risky speculative
  build, Items 58-66 are deferred as a group. No code changes; no
  rebuild/validation needed beyond the plan-doc edit itself (validated
  below per standing policy). Phase 6 is closed. Moving on to Phase 7
  (Items 67-76, pointer-array, aggregate, and VLA structural classes).

- **Items 67-76** (2026-08-02): Phase 7 close-out. Item 67's fresh
  census: exactly 1 `pointer-array` fallback function remains,
  `tptrarr.main`. Ran a forced-accept A/B
  (`DCC_MIR_FORCE_ACCEPT_FUNCTION=main`, `runall -Apps tptrarr -Mode
  full`): the app's output was correct, but performance regressed on
  every metric - peep cycles 1029081 -> 1039755 (+1.04%), nopeep
  cycles 1146734 -> 1171265 (+2.1%), nopeep bytes 7808 -> 7936. Per
  skill rule 5 ("correct-but-slow output is still fallback output
  until fixed"), this proves the gate is correctly blocking a genuinely
  worse candidate today, not just being over-cautious - even though
  the raw per-function census bytes looked smaller (9732 vs 10778),
  the actual assembled/measured app is bigger and slower. Building the
  full stride-aware lowering Item 68 describes (adapting
  `ast_index_deref_pointer_array_collect`'s ~150 lines of AST-level
  logic to MIR's IR) for a single already-confirmed-regressing
  candidate is disproportionate; deferred with this concrete A/B
  evidence, and Items 69/73 (fixtures for the deferred fix) deferred
  alongside it.

  Item 70: re-audited all VLA fallbacks (`tvla`/`tvlaparm`/`tvlax`) -
  every one falls back on plain `text-size`, the same shared cost
  cascade as the rest of the corpus; no distinct "VLA power-of-two
  gate" exists in the current code to audit separately (confirmed by
  grep - no VLA-specific fallback reason string exists anywhere in
  `dcc_mir.c`). These are simply members of the systemic ~2x-more-
  expensive spilled-path population the SKILL.md's "known root cause"
  section already documents; Phases 1-6's narrow, targeted fixes were
  never going to move this population, and didn't.

  Item 71: same finding for struct/aggregate functions
  (`tanonagg`/`tstruct`/`tstructi`/`tstructp`/`tstructv`) - all plain
  `text-size` fallback, no distinct "aggregate size ceiling" exists to
  extend. If anything this subpopulation is *worse* than the general
  ~2x gap (e.g. `tstructv.main`: 69050 generated vs 16079 captured
  bytes, a 4.3x gap) - not a near-cost candidate a small ceiling bump
  would help. Item 72 (verify `MIR_COPY_AGGREGATE`/struct-return
  slot-hygiene) is the same root-cause population by another name; a
  real fix here is the systemic spilled-path quality problem itself
  (the compare-materialize-then-retest class of bug the SKILL.md
  documents), which is explicitly out of scope for a single targeted
  item and needs its own dedicated, evidence-first investigation
  rather than a speculative "extend the ceiling" patch. Deferred
  alongside Item 71.

  No code changes this phase (Items 68/69/71/72/73 deferred with
  concrete evidence; 70 is a confirmed negative audit result). Item 74
  (full census) already captured above; Item 75 (VLA `-fstack-check`
  safety net) is N/A since no `mir.has_vla` function's emission
  changed. Item 76 milestone: `-Mode full -Extended` re-run per
  standing policy: 314/323 apps, 196/196 extended, all clean. Phase 7
  is closed. Moving on to Phase 8 (Items 77-84, large-CFG scaling &
  parked-selector audit).

- **Items 77-84** (2026-08-02): Phase 8 close-out - the biggest real win
  since Item 46. Item 77's fresh census: the same 3 `cfg-block-count`
  functions as Item 40 remain (`adaint.run` 107 blocks,
  `cobint.exec_range` 132 blocks, `fint.prim` 95 blocks), all far past
  the 64-block ceiling. Raising the ceiling enough to admit even the
  smallest of these (`fint.prim`, 95 blocks) would need nearly 1.5x the
  current limit, and the skill explicitly ranks large-CFG/inlining as
  the highest risk category after loops; with only 3 corpus functions
  at stake and no existing compile-time regression benchmark (Item 82
  would need to be built from scratch first), raising the ceiling now
  is a disproportionate risk for a marginal, unverified yield.
  Deferred; Items 81/82 are consequently N/A this phase (nothing to
  measure or benchmark without a ceiling change).

  **Item 78 (major finding)**: audited both parked selectors by git
  history and code reading.
  - `mir_try_emit_home_cfg_rollout` (~9382, pre-Item-80) gated a
    loop-phi structural subset but its body did nothing except call
    `mir_try_emit_homed_scalar_cfg()` directly - the exact function
    production already tries unconditionally for every function
    regardless of this gate. It could never produce output different
    from what the already-active production path produces for the
    same function - confirmed empirically via
    `DCC_MIR_EMIT_HOME_CFG=1` census: byte-for-byte identical to
    production for every function it matched. Pure dead, redundant
    scaffolding. **Removed** (Item 80) along with its 2 diagnostic
    call sites (`DCC_MIR_HOME_CFG_CANDIDATES`, `DCC_MIR_EMIT_HOME_CFG`)
    and the leftover env-var reference in `mir_begin_function`'s
    `report_mode` computation.
  - `mir_try_emit_general_rollout` (~9343) is a genuinely distinct,
    narrower selector for single-block, arithmetic-only, one-return
    functions, backed by `mir_try_emit_homed_scalar_dag()` (~5761) - a
    DAG-based emitter **completely unreachable in production**, used
    nowhere else in the codebase. Ran `DCC_MIR_EMIT_GENERAL=1` across
    the full census (28 functions structurally match) and cross-
    referenced every one against the production baseline: 26 of 27
    already-accepted functions got **smaller** output from
    `homed_scalar_dag` than production's `homed_scalar_cfg` (typically
    4-6 bytes smaller: e.g. `tasmcoll.test_1` 83 vs 87 bytes,
    `tdecl.add` 141 vs 146 bytes) - but not universally
    (`tinline.edge_unused` was *worse*, 121 vs 69 bytes), proving a
    blind swap would have been unsafe (skill rule 4: never assume
    smaller-and-different means better without comparing). Verified
    with a real forced full-app A/B (`DCC_MIR_EMIT_GENERAL=1 runall
    -Apps tdecl -Mode full`): correct output, and genuine measured
    cycle improvements (-0.36% peep, -0.66% nopeep), not just static
    bytes.

  **Item 79 (promotion)**: added `general-rollout` to the true
  production default path in `mir_end_function()`'s dispatch - tried
  alongside `homed-scalar-cfg` into a separate stream, keeping
  whichever candidate is smaller (or `general-rollout` alone if
  `homed-scalar-cfg` didn't match), using the same fresh-stream-swap
  pattern Item 46 established for the loop-family retry. This can
  never regress any function `homed-scalar-cfg` alone would have
  produced, since it's only replaced by a strictly smaller alternative.
  **Validation**: regression-gated census vs the Phase-7-end baseline:
  **0 regressions**, coverage unchanged at 174/2378 (a byte-size win
  within already-accepted functions, not new coverage - 28 functions
  moved from `homed-scalar-cfg` to `general-rollout`), 19 apps flagged
  for runtime validation. Focused `-Mode full` on all 19
  (`tasmcoll, tc89core, tc89decl, tc89fnty, tc89fp, tc99varm, tdead,
  tdecinit, tdecl, tforsco, tgnarly, tinline, tinlinfb, tkandr,
  tmirfast, tregnarw, ttype2, ttypesr, tvla`): **19/19 passed, 0
  regressions, 11 real measured cycle improvements** across 6 apps
  (`tasmcoll, tc89core, tc89fnty, tc89fp, tdecl, tforsco, tgnarly,
  tinlinfb` - peep and/or nopeep cycles down 0.01%-0.66%, byte counts
  unchanged or improved, no regressions on any metric). Accepted all
  19 improvements via `-UpdatePerfBaseline` (clean movement, every
  changed row strictly improved, nothing hidden). Milestone `-Mode
  full -Extended`: 314/323 apps, 196/196 extended, diagnostics/dccpeep/
  performance all clean.

  This is the second production-unlocking dispatch-wiring fix this
  plan has found (after Item 46's loop-family fix), and the pattern
  is now well-established: search for selectors reachable only via
  diagnostic env vars before assuming a fallback population needs a
  brand-new selector built from scratch. Phase 8 is closed. Moving on
  to Phase 9 (Items 85-92, cross-cutting hardening & multi-platform).

- **Items 85-92** (2026-08-02): Phase 9 close-out - cross-cutting
  hardening, mostly process/audit items with two real code
  improvements found along the way.

  **Item 85**: `scripts/mir-migration-census.py` had no way to force
  a global compiler flag across every compile (only per-app
  `dcc_args` overrides existed), so no census this whole plan had
  ever exercised `-fstack-check` - even though `runall.ps1`/`dccmake`
  build every app with it ON by default via `DCC_FORCE_STACK_CHECK`.
  Added a `--extra-args` option and ran a full census with
  `-fstack-check` forced, compared against the normal-mode baseline:
  coverage dropped from 174 to 168 (6 fewer `mir accepted`), all 6 in
  `tvla` (`vla_sizeof_*` functions), all falling back to `text-size`
  because the added stack-check prologue pushed their byte/cost gate
  just over threshold. This is not a bug: fallback is definitionally
  correct (byte-identical legacy replay), and `runall.ps1`'s normal
  correctness/perf runs (which build via `dccmake`, which does honor
  `DCC_FORCE_STACK_CHECK`) already exercise this exact configuration
  by default - confirmed by every full-extended run this session
  passing clean. No code change beyond the script's new flag.

  **Item 86**: grepped `dcc_mir.c` for duplicated frame-size formulas
  added/touched across Phases 1-8. Found two: `mir_try_emit_spilled_
  scalar_cfg`'s `frame_bytes = mir.local_bytes + mir.aggregate_temp_
  bytes + 2 * mir_prepare_backend_slots()` and `mir_try_emit_general_
  rollout`'s narrower `mir.local_bytes + 2 * mir_prepare_backend_
  slots()` (omitting `aggregate_temp_bytes`). Verified the omission
  was safe by construction - `general_rollout`'s opcode whitelist
  excludes `MIR_CALL` entirely, and `aggregate_temp_bytes` only ever
  accrues from anonymous struct call-argument temporaries, so it's
  provably always 0 in that domain - but consolidated anyway per
  Item 19's one-predicate discipline: extracted `mir_current_frame_
  bytes()` and pointed both call sites at it. Verified byte-for-byte
  identical census output before/after (0 changes across the whole
  corpus), proving the refactor is behavior-preserving.

  **Item 87**: `scripts/rtl-iy-safety.py` - clean, no IY reference
  anywhere in `DCCRTL.MAC`. `scripts/audit-runtime-coverage.py` -
  clean (195/195 standard API surface resolves to a covered runtime
  function; the 17 "unexpected" internal dispatch labels it flags are
  pre-existing formatted-I/O helper labels unrelated to and untouched
  by any Phases 1-8 commit, confirmed via `git diff --stat` showing
  `DCCRTL.MAC` was never touched this plan).

  **Item 88**: promoted from optional to required. CI (`ci.yml`) only
  ever built on `ubuntu-latest` (Linux/GCC). Verified the tooling was
  already Windows/MSVC-ready without needing it: `build-dcc.ps1`
  auto-detects MSVC via `vcvarsall` on Windows, and the `ntvcm`
  emulator repo ships `m.bat` alongside `m.sh`. Added a `windows-
  latest` matrix leg to `ci.yml` (`fail-fast: false` so one OS
  failing doesn't hide the other), split the ntvcm build step by
  `runner.os`, and rewrote the PATH-setup step from bash-only syntax
  (`$GITHUB_WORKSPACE`/`$GITHUB_PATH`) to `pwsh` with `$env:` prefixes
  so it works identically on both runners. Also added `-RunTimeout
  20` to CI's `runall.ps1 -Mode full -Extended` invocation, matching
  this session's established local practice, to bound any future
  hung test the same way the local timeout does. This cannot be
  fully verified locally (no Windows host in this environment); it
  is pushed for the real `windows-latest` GitHub Actions runner to
  validate, consistent with the standing practice of trusting CI
  after a local-verified push.

  **Item 89**: already de facto satisfied - every phase boundary
  this session (Items 45-46, 47-56, 57-66, 67-76, 77-84, and now
  85-92) ran a full `-Mode full -Extended` milestone pass before its
  commit, per standing instruction. Reconfirmed here with this
  phase's own clean run (314/323 apps, 196/196 extended).

  **Item 90**: `git diff --check` (whitespace-error scan) and
  `git fsck --no-progress --no-dangling` both clean on the working
  tree before this phase's push.

  **Item 91**: updated `.github/skills/mir-migration/SKILL.md` with
  two new sections capturing this session's real discoveries: (1)
  "selectors reachable only via a diagnostic env var" - documenting
  the Item 46 / Item 78-80 pattern and how to classify a found
  wrapper (distinct-emitter-promote vs. redundant-scaffolding-delete)
  before acting on it; (2) a discipline note on consolidating
  parallel cost-gate formulas as they accumulate, generalizing
  Item 19's lesson to the frame-size layer found again at Item 86.

  **Item 92 (milestone merge-readiness report)**: fresh full census -
  coverage 174/2378 functions (7.32%), selectors: spilled-scalar-cfg
  2253, homed-scalar-cfg 96, general-rollout 28, loop-family 1;
  fallback reasons: text-size 2146, instruction-count 27, inline-
  substitution 24, cfg-block-count 3, cfg-backedge 2, pointer-array 1,
  selector 1. 42 commits since Plan-100's first item (`e50ae51`)
  through this phase. Full `-Mode full -Extended`: 314/323 apps,
  196/196 extended, diagnostics/dccpeep/performance all clean, no
  regressions at any point across Phases 1-9. Two genuine production
  wins landed this plan beyond the original small-pattern-fold scope:
  Item 46's loop-family dispatch fix and Item 79's general-rollout
  promotion, both found by auditing for orphaned selectors rather
  than writing new ones - the highest-yield technique discovered
  across the whole plan. Phase 9 is closed. Moving on to Phase 10
  (Items 93-100, closing sweep & legacy-retirement readiness).

- **Items 93-100** (2026-08-02): Phase 10 close-out - the plan's
  closing sweep and legacy-retirement readiness assessment.

  **Item 93 (fresh final census)**: coverage 174/2378 functions
  (7.32%). Complete fallback-reason table: `text-size` 2146,
  `instruction-count` 27, `inline-substitution` 24, `cfg-block-count`
  3, `cfg-backedge` 2, `pointer-array` 1, `selector` 1 (2204 total
  fallback + 174 accepted = 2378).

  **Item 94 (per-reason classification)**:
  - `text-size` (2146, 90.2% of all fallback) - **genuinely
    unimplemented**, the dominant blocker. Root cause documented in
    SKILL.md's "Known root cause" section:
    `mir_emit_scalar_compare` unconditionally materializes an
    explicit 0/1 boolean for every comparison, which then gets
    spilled and reloaded just to be re-tested, and the existing
    narrow `mir_try_emit_comparison_branch` selector only fuses
    compare+branch for the single whole-function shape `if (param OP
    param) return A; return B;`, never the general case that
    `mir_try_emit_spilled_scalar_cfg` handles for 91% of the corpus.
    Fixing this needs a real new capability (a general compare+branch
    fusion selector, or fixing the double-materialization bug
    directly), not an incremental fold - Item-6/58-level scope.
  - `instruction-count` (27) - **intentional cost gate**, correctly
    blocking candidates whose generated code is measurably worse than
    legacy by count, with existing profiled-near-cost/byte-
    profitable/constant-bound-loop-pair exceptions already carved out
    in Phases 2-3. Re-audited: no new near-miss population found this
    plan (see Items 25-31's fast-path additions, which already mined
    this space).
  - `inline-substitution` (24) - **intentional gate** (Item 57-66's
    Phase 6 finding): the legacy static-inline call-site substitution
    mechanism is correct and byte-identical; MIR's call emission has
    no substitution pass. Building one is a from-scratch inliner
    (Item-6-level scope), deferred with rationale already documented.
  - `cfg-block-count` (3: `adaint.run`, `cobint.exec_range`,
    `fint.prim`) - **intentional gate**, re-confirmed unchanged since
    Item 40. Raising the 64-block ceiling for only 3 functions in the
    highest-risk category is a disproportionate risk (Item 77's
    finding).
  - `cfg-backedge` (2: `tregnarw.lres`, `tphijoin.loop_header_phi`) -
    **genuinely unimplemented**, but deliberately deferred: both were
    inspected in MIR detail (Item 52) and are genuinely different,
    unrelated loop shapes from the 4 specialized selectors Item 46
    already wired in; building a general arbitrary-loop selector with
    only 2 known targets and no shared pattern is Item-6-level
    ambiguity.
  - `pointer-array` (1: `tptrarr.main`) - **intentional gate, proven
    correct**: Item 67's forced A/B showed relaxing it would be a
    real performance regression (peep +1.04%, nopeep +2.1% cycles).
  - `selector` (1: `tgotocap.internal_label_loop`) - **intentional
    semantic-risk gate**: `DCC_MIR_SELECT_REPORT` traced this to an
    `implicit-return` preflight rejection - the function uses
    GNU computed-goto control flow that MIR's straight-line return-
    path preflight scan doesn't attempt to model, correctly declining
    rather than risk an unverified transformation (skill rule 5).

  **Items 95-96 (MIR-required-mode readiness)**: **not ready.** At
  7.32% coverage, 92.68% of functions still depend on legacy replay.
  The blocking classes for the next plan, in priority order by yield:
  (1) the systemic `text-size`/`spilled-scalar-cfg` population (2146
  functions, 90.2% of all fallback) - by far the highest-value target,
  requiring a genuinely new general compare-branch-fusion selector or
  a fix to `mir_emit_scalar_compare`'s double-materialization bug,
  both real engineering efforts, not incremental folds; (2) a from-
  scratch MIR-native static-inline substitution pass (24 functions);
  (3) a general arbitrary-loop selector once a second concrete
  `cfg-backedge` target with a shared pattern appears (currently only
  2, unrelated shapes); (4) the large-CFG ceiling (3 functions, low
  priority given the effort/yield ratio). This exact breakdown is the
  explicit handoff for a possible next 100-item plan (Item 100).

  **Item 97 (capture/replay skip measurement)**: investigated
  `dcc_func.c`'s `emit_function_epilogue` - legacy AST-driven Z80
  emission always runs in full for every function (writes directly
  to `g_emit_sink.stream`), and `mir_end_function()` only decides
  afterward whether to keep it or truncate-and-replace with MIR's
  output; there is no existing mode that skips legacy emission
  itself, only ones (`DCC_MIR_FORCE_FALLBACK`) that still run MIR in
  full and then discard its result. Timed 3 of the corpus's largest
  files (`adaint.c` 1172 lines, `cobint.c` 1302 lines, `tchess.c`
  1011 lines): 0.57s/0.79s/0.98s wall each, with only 0/67, 1/61, and
  2/39 of their functions MIR-accepted respectively. Since MIR
  acceptance is 0-5% even in the corpus's largest files, and legacy
  emission is architecturally interleaved with the AST walk's
  parsing/symbol/scope resolution (not a separable pass an accepted
  function could trivially skip), any wall-time saving from skipping
  capture for already-trusted functions is bounded well under 1-2% of
  total compile time today - not worth the engineering risk of
  breaking the safety-net comparison this migration's whole design
  relies on. Deferred; revisit only once coverage is substantially
  higher (e.g. >30-50%), where the bound becomes meaningfully larger.

  **Item 98 (whole-corpus performance vs. pre-Phase-1 baseline)**:
  diffed `tests/perf_baselines.csv` at the pre-Phase-1 commit
  (`e50ae51^`) against today's, 307 apps in common. Whole-corpus
  totals: peep cycles -0.000% (net neutral - the plan's wins are
  concentrated in a few apps, not spread thin), nopeep cycles
  -0.000%, peep size -0.020%. **18 apps improved, 0 regressed** -
  every single measured change across the whole plan was an
  improvement, never once a regression accepted into the baseline.
  Largest wins: `tcrcfix` -5.75% peep cycles (Item 46's loop-family
  fix), `tmircfg` -3.30%, `tc99scpe` -1.91%, plus 15 smaller
  improvements from Items 25-31's fast paths and Item 79's
  general-rollout promotion.

  **Item 99**: already current - every genuinely improved row was
  accepted into `tests/perf_baselines.csv` incrementally at its own
  phase boundary (Items 45-46, 77-84) via `-UpdatePerfBaseline` after
  a clean full-mode diff, never in bulk and never hiding a regression.

  **Item 100 (closing status report)**: Plan-100 is complete - all
  ten phases (Items 1-100) executed, documented, and pushed across 44
  commits from `e50ae51` through this entry. Final state: coverage
  174/2378 functions (7.32%), 0 regressions accepted at any point,
  18 apps with genuine measured performance improvements and 0 apps
  regressed against the pre-Phase-1 baseline. Two production-
  unlocking dispatch-wiring fixes (Items 46, 79) were found by
  auditing for orphaned `mir_try_emit_*` selectors reachable only via
  diagnostic env vars - the single highest-yield technique discovered
  across the whole plan, now documented in SKILL.md for reuse. The
  dominant remaining blocker for further coverage growth is the
  systemic `text-size` fallback population (90.2% of all fallback),
  which needs real new selector/emitter engineering, not further
  incremental pattern folds - this is the clear starting point for
  any next plan, which should re-derive its item list from a fresh
  census (per SKILL.md's own discipline) rather than continuing this
  plan's numbering. Handoff: search `dcc_mir.c` for `mir_try_emit_`
  functions not reachable from `mir_end_function()`'s default path
  before writing any new selector from scratch; then invest in a
  general compare-branch-fusion selector or a fix to
  `mir_emit_scalar_compare`'s double-materialization bug as the
  highest-value next step.
