# Compiler source coverage

Run `sh scripts/compiler-coverage.sh` from the repository root to build a
separate Clang-instrumented compiler and exercise it with:

- all main applications in both peephole and no-peephole modes;
- both stack-check and no-stack configurations of the main suite;
- the diagnostics suite;
- the dccpeep fixtures;
- all applicable C89, C99, and C11 extended single-exec tests in both modes;
- the MIR clobber suite, including qualifier and generated differential matrices;
- the MIR lifetime and required-emission suites; and
- instrumented host MIR verifier mutation tests.

The generated text and HTML reports are kept under
`build/compiler-coverage/report/` and are intentionally not committed.

The normal repository-root compiler is not replaced. The workflow routes
target builds and direct clobber assertions through `DCC`, builds the host
verifier with the same Clang instrumentation, and combines both executables'
coverage mappings. `%p-%m` profile names distinguish process IDs and binary
signatures. Each run removes old raw profiles before collecting fresh ones.

## Legacy-excluded AST/MIR report

Use `ast-mir-function-summary.txt` for the function-scoped headline described
below. The original `ast-mir-summary.txt` remains a module-scoped baseline, with
`ast-mir-sources.txt` recording its exact source-file denominator and
`ast-mir-coverage.json` holding the raw LLVM export. LLVM can include functions
outside the requested files in that export; use the verified function summary
JSON, not the raw export's function list, for scoped aggregates.
`summary.txt` and `html/index.html` are unfiltered collection artifacts, not
coverage targets.

The scoped report includes:

- AST storage, parsing, metadata, and statement metadata modules:
   `dcc_ast.c`, `dcc_ast_build.c`, `dcc_ast_metadata.c`, and `dcc_ast_stmt_meta.c`;
- MIR lowering, verification, allocation, selection, streams, and production
   emitters, including the active `dcc_mir_machine_*.c` schedules.

It excludes the legacy direct-codegen modules, all mixed `dcc_ast_gen*.c`
modules, and the optional `dcc_mir_schedule.c` / `dcc_mir_target.c` shadow
models. The separate function-scoped report adds classified active AST helpers.
This historical baseline is
an active-owner **module** report, not a complete production-only function
classification. Diagnostics, defensive checks, and unused helpers within
included modules remain in the denominator. Legacy codegen is not a target
for coverage-driven test additions.

`scripts/ast-mir-coverage.tsv` is the versioned module classification: 28
active-owner modules, four function-classified mixed modules,
and two optional diagnostic modules. Each row includes its rationale. The
coverage runner validates it before building: missing files, duplicate entries,
invalid categories, and newly added unclassified AST/MIR modules fail rather
than silently changing the denominator. No classification is based on whether
the current tests happened to execute a function.

Validate the classification independently with:

```sh
sh scripts/coverage-sources.sh
sh scripts/test-coverage-sources.sh
```

The module baseline intentionally stays unchanged as function classification
adds mixed-module helpers to the separate report below.

Measured on macOS with Apple Clang 21, on 2026-09-07, using compiler revision
`9ad4775e` plus the coverage-routing and host-test changes documented here:

| Metric | Covered | Total | Coverage |
| --- | ---: | ---: | ---: |
| Functions | 3,885 | 4,210 | 92.28% |
| Lines | 162,690 | 184,817 | 88.03% |
| Branch outcomes | 80,947 | 138,322 | 58.52% |
| Regions | 154,151 | 173,356 | 88.92% |

Selected active module results:

| Module | Lines | Branch outcomes |
| --- | ---: | ---: |
| `dcc_ast_build.c` | 88.71% | 74.49% |
| `dcc_ast_metadata.c` | 85.14% | 76.57% |
| `dcc_ast_stmt_meta.c` | 74.70% | 63.68% |
| `dcc_mir.c` | 84.72% | 72.58% |
| `dcc_mir_verify.c` | 98.31% | 95.21% |
| `dcc_mir_select.c` | 67.83% | 56.31% |
| `dcc_mir_homed_cfg.c` | 88.07% | 78.15% |
| `dcc_mir_spilled_cfg.c` | 92.34% | 68.74% |

The run passed both main configurations, diagnostics, extended tests, MIR
regressions, and host mutation tests. The main suite passed 481 applications
and skipped 24 in each configuration, with zero checked performance
regressions. Three coverage-guided host cases check unreachable definitions
feeding a reachable PHI, a branch-local argument at a join call, and a valid
late PHI. They increase verifier branch coverage from 93.84% to 95.21% without
changing compiler behavior.

These percentages measure exercised source, not C semantic completeness or
the percentage of bugs removed. Uncovered branches should be inspected for
meaningful supported cases, not executed merely to improve the total.
Prioritize active qualifier/call contracts and selector rejection proofs;
retain the existing separate tests of volatile access counts and widths.

## Function-scoped denominator

`scripts/ast-function-coverage.json` explicitly classifies every definition in
the four mixed modules. Clang's JSON AST supplies definitions and references;
prototypes and header definitions are not counted. New or removed definitions,
duplicate entries, and changes to guarded production-to-legacy references fail
validation. CI runs this check independently of expensive coverage collection.

| Mixed module | Production | Legacy-only |
| --- | ---: | ---: |
| `dcc_ast_gen.c` | 87 | 7 |
| `dcc_ast_gen_cond.c` | 29 | 27 |
| `dcc_ast_gen_expr.c` | 14 | 73 |
| `dcc_ast_gen_support.c` | 41 | 8 |
| Total | 171 | 115 |

Production roots were traced from `dcc_ast_metadata.c`, `dcc_ast_stmt_meta.c`,
`dcc_mir.c`, `dcc_ast_build.c`, `dcc_func.c`, `dcc_decl.c`, and `dcc_stmt.c`:
statement/support gates, condition and type/address proofs, loop metadata,
initializer capture, VLA-bound expressions, and inline metadata. Their
transitive mixed-module helpers are included even when unexecuted. Remaining
definitions belong to retained emission and emitter-only proofs. Classification
is not inferred from function names or measured execution counts.

Seven mixed-module references cross from included functions to excluded
emitters. The manifest records each guard: initializer capture returns when
MIR is active; discarded expressions use MIR instead of dead-expression
emission; inline metadata passes `emit_values=0`. The analogous external
`emit_init_auto_struct_type` reference to `ast_gen_expr` also follows a MIR
capture path that bypasses emission. These are reviewed control-flow arguments,
not a whole-program reachability proof. The validator detects changed reference
sets, not edits to the guards themselves. Positive execution of an excluded
function is a hard coverage-workflow error and requires reclassification.

Included functions remain whole: their diagnostics, defensive checks, and
guarded legacy branches are not removed to raise percentages. The 28
active-owner modules likewise retain all their compiled functions. Thus the
denominator excludes legacy-only functions in mixed modules but is deliberately
conservative, not a claim that every included line is production-reachable.

Artifacts:

- `ast-mir-functions.txt`: exact LLVM function-name allowlist.
- `ast-mir-function-detail.txt`: native LLVM per-function line/region/branch metrics.
- `ast-mir-function-coverage.json`: verified per-function metrics and summed totals.
- `ast-mir-function-summary.txt`: readable headline totals.

LLVM file reports and JSON exports do not apply function-name filters. This
workflow uses `llvm-cov report -show-functions` with an `[llvmcov]` allowlist,
then checks that every reported name matches the expected set exactly before
summing native metrics. It does not reconstruct executable lines from source
text or coverage regions. Line totals are function-summed and should only be
compared with subsequent reports using the same convention and classification.

Measured on 2026-09-07 with Apple Clang 21 and the same compiler/workload as the
module baseline (classification changes do not change compiler behavior):

| Metric | Covered | Total | Coverage |
| --- | ---: | ---: | ---: |
| Functions | 4,055 | 4,381 | 92.56% |
| Lines | 167,417 | 190,418 | 87.92% |
| Branch outcomes | 85,635 | 145,185 | 58.98% |
| Regions | 162,334 | 182,782 | 88.81% |

```sh
python3 scripts/ast-function-coverage.py
python3 -m unittest discover -s scripts/tests -p 'test_ast_function_coverage.py'
```

## September 8 Correctness Follow-up

The assertion-backed follow-up adds seeded differential programs, strict
near-match rejection checks, invalid-IR mutation sweeps, and isolated compiler
mutation controls. It found and fixed a real definition-cache invalidation bug
inside object promotion. The same source denominator is retained; three
invalidation calls add three executable lines.

| Metric | Before | After |
| --- | ---: | ---: |
| Lines | 167,478 / 190,462 (87.93%) | 167,485 / 190,465 (87.93%) |
| Branch outcomes | 85,685 / 145,239 (59.00%) | 85,714 / 145,239 (59.02%) |
| Functions | 4,057 / 4,383 (92.56%) | 4,057 / 4,383 (92.56%) |

`mir_verify_dominance` itself reaches 214/214 executable lines, 168/168 regions,
and 125/126 branch outcomes. The remaining true outcome of `incoming == 0`
is reviewed in `scripts/ast-coverage-reviews.json`: evaluation reaches it only
for a reachable non-entry PHI block, which necessarily has a reachable incoming
edge. The entry-PHI error is tested separately. This defensive guard remains in
both source and coverage totals; it is not deleted or excluded for a percentage.

`ast-mir-gaps.json` records uncovered branch outcomes with exact source, function,
line/column, and true/false identity. Review annotations require an unchanged
source-expression anchor and evidence. The ledger includes 59,341 distinct raw
LLVM branch records, of which 59,340 remain unreviewed, plus 326 unexecuted
functions. Raw branch records are not interchangeable with LLVM's native
function-summary branch denominator (which also accounts for folded/expanded
coverage); the headline continues to use native metrics.

The 115 previously classified legacy-only functions were checked against the
new workload: none executed. Mixed initializer/inline functions remain included
in full, including their guarded legacy branches. The optional shadow modules
remain outside the production-owner report for architectural reasons, not for
low execution counts. No new exclusion was introduced.

Both strict full+extended release gates passed (481 applications, 24 documented
skips per configuration), with zero checked performance regressions and no
baseline edits. The full coverage/MIR workflow, sanitizer host tests, generator
replay checks, three compiler-mutation controls, script tests, and debugger-host
tests passed locally. New cross-platform CI steps are configured but have not
been run remotely for this uncommitted follow-up.

### Remaining Completion Gates

- Review the remaining 59,340 raw uncovered outcomes rather than labeling them
   unreachable by default; add supported-input or malformed-IR assertions as needed.
- Cover the 326 unexecuted included functions or justify their classification.
- Extend near-match/generic equivalence beyond the five enforced schedule families.
- Extend the seeded grammar beyond bounded unsigned arithmetic and current memory/call forms.
- Add compiler mutants beyond the two verifier controls and promotion-cache
   regression, and investigate survivors.

This follow-up completes neither exhaustive source coverage nor the complete
exclusion audit. It supplies reproducible tests and an explicit backlog so those
requirements cannot silently disappear behind a rounded percentage.

## Historical Unfiltered Report

The first full run on 2026-08-28 produced:

| Metric | Covered |
| --- | ---: |
| Functions | 88.48% |
| Lines | 83.91% |
| Branches | 56.77% |
| Regions | 84.60% |

All 473 runnable main applications and all 196 applicable extended tests
passed in both optimization modes during this run. The 12 main-suite and 23
extended-suite skips remained the documented target or dialect exclusions.

This historical total includes legacy and shadow paths and uses a different
workload and denominator. Do not compare it directly with the scoped report
above or use it as a pass/fail threshold.
