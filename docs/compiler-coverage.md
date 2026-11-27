# Compiler coverage

Build the DCC tools and make `ntvcm` available on `PATH`. From the repository
root, run:

```sh
sh scripts/compiler-coverage.sh
```

The driver builds a separate Clang-instrumented compiler and runs the main
`runall.py` suite with and without stack checks, the extended suite, generated
MIR fuzz tests, debug compile censuses, and the host MIR tests. The target
runners use the instrumented compiler through `DCC`.

Set `CC`, `LLVM_COV`, and `LLVM_PROFDATA` to use a matching LLVM toolchain.
`DCC_COVERAGE_BUILD_DIR` defaults to `build/compiler-coverage`.
`DCC_COVERAGE_JOBS` controls the main test parallelism,
`DCC_COVERAGE_CLOBBER_JOBS` controls MIR fuzz parallelism, and
`DCC_COVERAGE_CENSUS_JOBS` controls debug census parallelism.

The three stages can also run separately:

```sh
DCC_COVERAGE_STAGE=build sh scripts/compiler-coverage.sh
DCC_COVERAGE_STAGE=collect sh scripts/compiler-coverage.sh
DCC_COVERAGE_STAGE=report sh scripts/compiler-coverage.sh
```

Collection starts with fresh profiles and records the completed MIR fuzz
configurations. Report mode verifies the recorded inputs, toolchain,
binaries, profiles, and execution manifest before exporting coverage.

## Reports

Under `build/compiler-coverage/report/`:

- `summary.txt` and `html/index.html` provide LLVM's native summary and source view.
- `compiler-function-summary.txt` reconciles whole-compiler source-function counts.
- `ast-mir-summary.txt` uses the source-file denominator in `ast-mir-sources.txt`.
- `ast-mir-function-summary.txt` uses the function-scoped denominator below.

The AST/MIR module list is maintained in `scripts/ast-mir-coverage.tsv`.
Validate changes with `sh scripts/coverage-sources.sh` and
`sh scripts/test-coverage-sources.sh`.

To reconcile whole-compiler branch coverage from a completed report:

```sh
python3 scripts/compiler-branch-coverage.py \
  --inventory build/compiler-coverage/report/compiler-function-inventory.json \
  --coverage build/compiler-coverage/report/compiler-coverage.json \
  --native-report build/compiler-coverage/report/summary.txt \
  --output-dir build/compiler-coverage/report
```

Use `--allow-below-target` for a report below the branch target.

## Function-scoped denominator

`scripts/ast-function-coverage.json` explicitly classifies every definition in
the four mixed modules. Clang's JSON AST supplies definitions and references;
prototypes and header definitions are not counted. New or removed definitions,
duplicate entries, and changes to guarded production-to-legacy references fail
validation. CI runs this check independently of expensive coverage collection.

| Mixed module | Production | Legacy-only |
| --- | ---: | ---: |
| `dcc_ast_classify.c` | 88 | 0 |
| `dcc_ast_stmt_classify.c` | 30 | 0 |
| `dcc_ast_capture.c` | 14 | 0 |
| `dcc_ast_support.c` | 58 | 0 |
| Total | 190 | 0 |

Production roots were traced from `dcc_ast_metadata.c`, `dcc_ast_stmt_meta.c`,
`dcc_mir.c`, `dcc_ast_build.c`, `dcc_func.c`, `dcc_decl.c`, and `dcc_stmt.c`:
statement/support gates, condition and type/address proofs, loop metadata,
initializer capture, VLA-bound expressions, and inline metadata. Their
transitive mixed-module helpers are included even when unexecuted. Remaining
definitions belong to retained emission and emitter-only proofs. Classification
is not inferred from function names or measured execution counts.

No legacy-only definitions remain in the mixed modules, so the manifest's
`guarded_edges` list is empty. If a definition is classified `legacy` again,
every reference to it from an included function must be recorded there with the
guard that keeps it from executing; the validator rejects changed reference
sets. Positive execution of an excluded function is a hard coverage-workflow
error and requires reclassification.

Included functions remain whole: their diagnostics, defensive checks, and
guarded legacy branches are not removed to raise percentages. The 29
active-owner modules likewise retain all their compiled functions. Thus the
denominator excludes legacy-only functions in mixed modules but is deliberately
conservative, not a claim that every included line is production-reachable.

Artifacts:

- `ast-mir-functions.txt`: exact LLVM function-name allowlist.
- `ast-mir-function-detail.txt`: native LLVM per-function line/region/branch metrics.
- `ast-mir-function-coverage.json`: verified per-function metrics and summed totals.
- `ast-mir-function-summary.txt`: readable headline totals.
- `ast-mir-gaps.json`: unexecuted functions, zero-count source-region anchors,
  and uncovered branch outcomes for the selected function set.

LLVM file reports and JSON exports do not apply function-name filters. This
workflow uses `llvm-cov report -show-functions` with an `[llvmcov]` allowlist,
then checks that every reported name matches the expected set exactly before
summing native metrics. It does not reconstruct executable lines from source
text or coverage regions. Line totals are function-summed and should only be
compared with subsequent reports using the same convention and classification.

LLVM 18 can include mixed-module legacy functions in the native
`-show-functions` report even though none appears in the generated name
allowlist; none are currently classified legacy. The analyzer derives that exact exclusion set from the same raw
coverage identities and versioned classification, ignores only those known
legacy rows when summing native metrics, and still fails if any excluded
function executed. Unexpected functions, duplicate rows, selected/excluded
overlap, missing selected functions, and changed classifications remain hard
errors. This is tool-output compatibility, not a denominator change.

```sh
python3 scripts/ast-function-coverage.py
python3 -m unittest discover -s scripts/tests -p 'test_ast_function_coverage.py'
```

Set `DCC_COVERAGE_REQUIRE_COMPLETE=1` when invoking
`scripts/compiler-coverage.sh` to require exact equality for functions, lines,
native branch outcomes, and regions in the selected AST/MIR report, and full
function coverage across the compiler. By default, the reports show gaps
without requiring full coverage.
