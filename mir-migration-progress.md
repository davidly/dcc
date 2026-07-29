# MIR Migration Progress

This document records the current staged migration of dcc from direct legacy AST-to-Z80 assembly emission to a whole-function MIR (middle IR) pipeline.

The migration is deliberately transactional: every eligible function is lowered to MIR, but MIR output is selected only when it is correct and performance-competitive. Otherwise, the compiler replays the established legacy emitter for that function.

This keeps the new MIR path active in normal builds without turning an incomplete migration into a correctness or performance risk.

## Work Completed

### MIR Infrastructure And Staged Rollout

- Implemented the core MIR pipeline in `src/dcc/dcc_mir.c`:
  - whole-function lowering from the parsed AST;
  - CFG construction;
  - value and type metadata propagation;
  - liveness analysis and backend-slot allocation;
  - register/spill-aware emission;
  - transactional capture of legacy output and per-function replay fallback.
- Added selectors for profitable scalar and CFG-shaped functions, including homed and spilled scalar paths.
- Kept the legacy AST emitter fully available as the fallback backend, so the migration can proceed incrementally rather than requiring a risky all-at-once switch.
- Added detailed selector reporting through `DCC_MIR_SELECT_REPORT=1`, including selector name, acceptance/fallback reason, generated versus captured text size, instruction counts, and CFG block count.
- Added per-function diagnostic controls for controlled A/B investigation:
  - `DCC_MIR_FUNCTION`
  - `DCC_MIR_FORCE_ACCEPT_FUNCTION`
  - `DCC_MIR_FORCE_FALLBACK_FUNCTION`
  - `DCC_MIR_FORCE_FALLBACK`
  - `DCC_MIR_COVERAGE`
  - `DCC_MIR_REQUIRE_COMPLETE`

### MIR Correctness And Code Generation

- Repaired a broad set of lowering and ABI issues discovered while exercising real applications:
  - parameter and local metadata;
  - pointer and array representation;
  - lexical scope and symbol-renaming interactions;
  - VLA `sizeof` and allocation behavior;
  - pointer arithmetic;
  - call argument width and ABI placement;
  - standard stream lowering (`stdin`, `stdout`, `stderr`);
  - signed comparison handling;
  - unary constant folding;
  - representation-identical conversion folding;
  - assignment-alias folding.
- Reduced unnecessary frame and spill overhead:
  - do not allocate backend slots for unused values;
  - omit dead parameter/load emission when no slot is needed;
  - omit zero-byte spilled-frame setup;
  - remove unnecessary register preservation;
  - eliminate call-only constant slots;
  - rematerialize bounded simple call arguments directly.
- Added scalar division/modulo fusion so matching `/` and `%` operations use the established `__sdivmod` / `__udivmod` runtime path rather than duplicating work.
- Added profitable power-of-two multiplication lowering where appropriate.
- Preserved a targeted VLA power-of-two fallback gate after profiling showed that apparently smaller MIR output could still lose in no-peephole execution because of spill/frame traffic.

### Performance-Aware Admission Policy

- Made selector admission depend on measured behavior rather than assuming that shorter assembly text or fewer instructions is automatically better.
- Added carefully profiled structural admissions for:
  - equal-cost zero-frame spilled CFGs;
  - low-block homed CFG near-misses;
  - near-identical inline-call-marked candidates;
  - simple acyclic scalar/CFG cases.
- Kept known unprofitable shapes on fallback instead of weakening the gate globally.
- Avoided application-specific selector exceptions; acceptance remains structural and reproducible.

### Test Runner, Runtime, And Optimizer Hardening

- Improved `scripts/runall.ps1` for practical iterative compiler work:
  - `-Apps` filtering;
  - configurable `-RunTimeout`;
  - serial mode;
  - bounded child-process cleanup;
  - support for focused full-mode validation.
- Repaired runtime behavior in `DCCRTL.MAC`, including work around `_exec`/FCB-related behavior encountered during validation.
- Strengthened `dccpeep` subtract-one optimization safety using control-flow/dataflow checks and added regression fixtures in `tests/dccpeep`.
- Added MIR-specific regression programs and baselines, including `tmircfg` and `tiyreg`.
- Updated performance baselines only after verified measurements; they were not used to conceal selector regressions.

### Migration Automation And Workflow Skill

- Added `scripts/mir-migration-census.py`, which:
  - compiles the runnable corpus with selector reporting enabled;
  - creates stable TSV snapshots;
  - compares rollout states between snapshots;
  - identifies newly emitted and returned-to-fallback functions;
  - separates fallback-metric churn from runtime-relevant MIR changes;
  - generates focused `runall.ps1 -Apps` validation sets;
  - understands test overrides, compiler arguments, ignored applications, filtered corpus runs, and regression checks.
- Added the project-specific MIR migration workflow skill in `.github/skills/mir-migration/SKILL.md`, registered in `.github/skills/README.md`.

The skill captures the operating discipline established during this work so that further MIR rollout can be continued consistently by one or several contributors. It covers:

- finding and categorising fallback reasons through `DCC_MIR_SELECT_REPORT`;
- taking and comparing corpus snapshots with `scripts/mir-migration-census.py`;
- selecting one small, measurable batch of candidates rather than widening rollout blindly;
- using force-accept and force-fallback controls only for diagnosis and A/B profiling;
- treating text size and instruction count as preliminary filters, with peep and no-peep emulator measurements as the final admission criterion;
- applying structural selector gates rather than application-specific exceptions;
- running focused full-mode validation for affected applications, then periodic full-suite milestones;
- recording negative profiling outcomes, particularly where MIR output is smaller but slower;
- coordinating branch ownership, handoffs, commits, and eventual retirement conditions for the legacy capture/replay backend.

The skill makes the staged migration repeatable: new work can start from a corpus census, target a known fallback category, validate only the affected applications during development, and preserve the same correctness and performance standards that have governed the current rollout.

## Current Rollout Status

The migration is intentionally partial, but it is active in production compiler output.

- **129 of 2,319 runnable-corpus functions emit through MIR: 5.56%.**
- MIR lowering has broad corpus coverage; the remaining gap is principally competitive emission and selector admission, not an inability to represent source semantics.
- The dominant fallback category is assembly text-size/cost gating:

| Fallback reason | Functions |
| --- | ---: |
| Text-size gate | 2,131 |
| Inline substitution | 22 |
| Instruction-count gate | 19 |
| VLA power-of-two performance gate | 10 |
| CFG backedge | 5 |
| CFG block-count limit | 2 |
| Pointer-array handling | 1 |

Fallback is a normal, safe state. The compiler actively emits MIR where the new path earns its place and uses proven legacy output elsewhere.

## Benefits

- **Lower migration risk:** the compiler can ship and be merged while MIR development continues. A noncompetitive or unsupported function falls back locally instead of blocking the entire program.
- **Continuous production exposure:** active MIR functions are compiled and tested in normal workloads now, exposing issues early while preserving user-visible behavior.
- **Better engineering visibility:** selector provenance, cost data, snapshots, and per-function A/B controls turn migration decisions into measured evidence instead of guesswork.
- **Performance protection:** acceptance is validated in both peephole and no-peephole configurations. This catches cases where compact-looking output is slower on the emulator.
- **Improved compiler maintainability:** whole-function CFG, liveness, value metadata, and allocation form a stronger foundation for future optimizations than direct syntax-tree emission alone.
- **Faster iteration:** focused app runs and migration census output make it practical to investigate one real candidate at a time without repeatedly paying for a full suite.
- **Safer optimizer/runtime behavior:** the branch includes concrete fixes and regression coverage outside the MIR core, particularly runtime behavior and `dccpeep` transformation safety.
- **Clear multi-contributor process:** the migration skill and census tool make it possible to split selector, lowering, performance, and test work without losing validation discipline.

## Validation

The latest full merge gate completed successfully at the current branch head:

```text
Applications: 310 passed, 0 failed, 9 skipped
Diagnostics: 106 / 106 passed
dccpeep fixtures: 17 / 17 passed
Performance checks: passed
```

The most recent focused MIR admission batch also completed in full mode:

```text
16 affected applications validated
25 measured improvements
0 regressions
```

Additional merge checks completed successfully:

- `git diff --check main...HEAD` reported no whitespace errors.
- `git fsck --no-progress --no-dangling` completed without repository errors.
- No generated build artifacts are included in the tracked branch changes.
- Local `main`, `origin/main`, and `upstream/main` were synchronized at `05b5421`.
- The branch was 61 commits ahead and 0 behind `main`; `main` was an ancestor of the branch.

## Remaining Work

The migration is not complete. The next stages should focus on increasing MIR coverage without weakening the measured cost and correctness gates.

1. **Improve general spilled emission.** Most fallback functions are rejected because current spilled MIR output is larger or more expensive than legacy output. Work should target frame layout, spill traffic, temporary lifetime handling, register reuse, and call-clobber decisions.

2. **Add cost-aware static-inline handling.** Inline substitution accounts for 22 fallbacks. A direct MIR-side expansion prototype enlarged nested bodies into larger CFGs and did not admit callers. The next implementation needs a cost model and likely a selective strategy rather than unconditional inlining.

3. **Support loops and backedges competitively.** Five functions currently fall back due to CFG backedges. MIR needs loop-aware allocation and emission that can compete with the legacy backend's effective use of BC, IY, and existing loop patterns.

4. **Finish pointer-to-array handling.** The remaining pointer-array gate requires stride-aware lowering/emission and validation against real array-indexing applications.

5. **Raise large-CFG limits carefully.** Two functions currently hit the CFG block-count gate. This should be expanded in measured increments, after confirming code-size and emulator behavior remain competitive.

6. **Continue targeted VLA work.** The ten VLA power-of-two fallbacks are intentional. They should remain on legacy output until spill/frame improvements make the MIR variant competitive in both peephole and no-peephole modes.

7. **Expand CI confidence before retiring legacy emission.** Add a MIR-required validation stage once coverage is high enough, then progressively tighten eligibility. The legacy capture/replay emitter should remain until corpus coverage and performance evidence support removing it.

8. **Optional additional merge assurance.** The current merge gate was run on macOS. Running the same current-head suite on Windows/MSVC and Linux/GCC, plus the extended imported corpus, would provide additional platform confidence but is not a technical blocker for this branch.

## Merge Status

The branch was technically ready to merge at this checkpoint. It was ahead of `main`, had no merge-base divergence, passed the full current-head test/performance gate, and contained no tracked generated artifacts.

The merge could be a fast-forward:

```sh
git switch main
git merge --ff-only perf/unified-regalloc
```

The only untracked item in the worktree at that checkpoint was `dcc-regalloc-review.docx`; it is unrelated and should remain outside the merge.
