# Compiler source coverage

Run `sh scripts/compiler-coverage.sh` from the repository root to build a
separate Clang-instrumented compiler and exercise it with:

- all main applications in both peephole and no-peephole modes;
- both stack-check and no-stack configurations of the main suite;
- the diagnostics suite;
- the dccpeep fixtures;
- all applicable C89, C99, and C11 extended single-exec tests in both modes;
- the MIR clobber suite, including qualifier and generated differential matrices;
- the MIR lifetime and required-emission suites;
- full-corpus `-g` and `-gline` compile censuses with and without stack checks;
  and
- instrumented host MIR verifier mutation tests.

The clobber suite writes
`build/compiler-coverage/report/mir-clobber-executions.json`. It rejects a
full or focused run unless every expected stack/no-stack, peep/nopeep, and
debug configuration actually completed. Forced-candidate controls also assert
the selected candidate name from `DCC_MIR_COST_REPORT`, not only the shared
selector class. This prevents an empty focused case or a different
homed/spilled candidate from producing success-shaped coverage.

The generated text and HTML reports are kept under
`build/compiler-coverage/report/` and are intentionally not committed.

The normal repository-root compiler is not replaced. The workflow routes
target builds and direct clobber assertions through `DCC`, builds the host
verifier with the same Clang instrumentation, and combines both executables'
coverage mappings. LLVM's `%8m` online merge pool keeps repeated short-lived
compiler processes from overwriting profiles when the host reuses a PID, while
the binary signature keeps compiler and verifier data distinct. Each run
removes old raw profiles before collecting fresh ones.
Relative `DCC_COVERAGE_BUILD_DIR` values are normalized against the repository
root so CTest's build-directory working directory cannot redirect host-verifier
profiles into a nested, unmerged path.
The main runner also passes `DCC` through to the diagnostics suite; do not
replace that route with a hard-coded repository-root compiler or diagnostic
AST rejection paths disappear from coverage. `test-ast-dump.ps1` separately
exercises the opt-in AST diagnostic renderer with content assertions.

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
- `ast-mir-gaps.json`: unexecuted functions, zero-count source-region anchors,
  and uncovered branch outcomes for the selected function set.

LLVM file reports and JSON exports do not apply function-name filters. This
workflow uses `llvm-cov report -show-functions` with an `[llvmcov]` allowlist,
then checks that every reported name matches the expected set exactly before
summing native metrics. It does not reconstruct executable lines from source
text or coverage regions. Line totals are function-summed and should only be
compared with subsequent reports using the same convention and classification.

LLVM 18 can include the 115 mixed-module legacy functions in the native
`-show-functions` report even though none appears in the generated name
allowlist. The analyzer derives that exact exclusion set from the same raw
coverage identities and versioned classification, ignores only those known
legacy rows when summing native metrics, and still fails if any excluded
function executed. Unexpected functions, duplicate rows, selected/excluded
overlap, missing selected functions, and changed classifications remain hard
errors. This is tool-output compatibility, not a denominator change.

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

Set `DCC_COVERAGE_REQUIRE_COMPLETE=1` when invoking
`scripts/compiler-coverage.sh` to require exact equality for functions, lines,
native branch outcomes, and regions. The workflow writes its reports before
failing so an incomplete run remains actionable. This gate does not treat
review annotations as covered and does not round percentages.

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

`mir_verify_dominance` itself reached 214/214 executable lines, 168/168 regions,
and 125/126 branch outcomes at this historical checkpoint. The remaining
`incoming == 0` outcome was reviewed as redundant: evaluation checked only a
reachable non-entry PHI block, which necessarily had a reachable incoming edge.
After the user authorized justified source deletion, that disjunct and its
otherwise-unused counter were removed. The separately tested `start == 0`
entry-PHI rejection and every predecessor/dominance check remain.

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

The next local checkpoint adds call-level arity verification with fixed,
variadic, and unprototyped controls. Six malformed-call cases were accepted
before the change; all are now rejected. Local callback metadata now retains
the variadic flag. This is verifier-only validation, not a change to valid
call emission. Both strict release gates passed with zero checked performance
regressions, as did sanitizer host tests, the complete MIR suites, 81 script
tests, and all 10 debugger-host tests. The aggregate coverage figures above
predate this follow-up and have not been remeasured for the new verifier code.

Compiler mutation testing now includes call arity, for four controls total.
The added mutation exposed timestamp-dependent object reuse in the incremental
mutation builds. Each mutation now forces a clean rebuild and must produce its
own expected assertion failure. All four controls passed that stricter check;
the earlier three-control measurements should not be treated as a broad mutation
score. Remote CI has not run for these local changes.

## September 8 CLI continuation

The continuation from merged PR #193 was measured on Linux with Ubuntu Clang
18.1.3. The isolated instrumented compiler and host verifier passed both main
configurations, all applicable extended tests, clobber/lifetime/required-
emission tests, and the host verifier. The function-scoped result was:

| Metric | Covered / total | Percent |
| --- | --- | --- |
| Lines | 167,624 / 190,636 | 87.93% |
| Native branch outcomes | 85,629 / 144,936 | 59.08% |
| Functions | 4,058 / 4,384 | 92.56% |
| Regions | 151,473 / 170,483 | 88.85% |

The raw ledger has 59,001 uncovered branch outcomes: one retained reviewed
defensive outcome and 59,000 unreviewed outcomes. It still lists 326
unexecuted included functions. The new included function is the shared call-
prototype resolver; the new target regression executes normally but is not
part of the host compiler-function denominator.

This run added no exclusions and did not change any existing performance
baseline. The only new baseline row belongs to the new `tfpshad` workload.
The totals remain far from 100%; they are a fresh checkpoint for prioritizing
the next assertion-backed gap, not completion evidence.

Routing diagnostics through the instrumented compiler and adding direct AST
dump plus MIR stream block-I/O tests raises the next checkpoint to 4,063/4,384
functions, 167,810/190,636 lines, 85,813/144,936 native branch outcomes, and
151,629/170,483 regions. Unexecuted functions fall from 326 to 321 and
unreviewed raw outcomes from 59,000 to 58,816. Two permanent diagnostics cover
the remaining do-while and generic-statement message cases. No source or
function classification changed.

The following host/matrix batch exercises safe MIR query defaults and scope
restoration, parameter/IY emitters, isolated field resolution, five-argument
recovery, and every fixed spilled candidate with exact diagnostic/control
output equivalence. Coverage is now 4,103/4,384 functions,
168,353/190,636 lines, 86,027/144,936 native branch outcomes, and
152,004/170,483 regions. The raw ledger has 58,603 uncovered outcomes, one
reviewed and 58,602 unreviewed, plus 281 unexecuted included functions.

Candidate-state default assertions then cover 40 homed/spilled feature-query
functions and ensure no state leaks between attempts. Valid transformation
fixtures exercise direct PHI-return forwarding and both block/region common
address elimination, including source-value rewrites and post-transform
verification. Coverage reaches 4,152/4,384 functions, 168,691/190,636 lines,
86,139/144,936 native branch outcomes, and 152,238/170,483 regions. The raw
ledger has 58,491 uncovered outcomes, one reviewed and 58,490 unreviewed, plus
232 unexecuted included functions.

An evidence-backed exact-schedule review then restored
`tptrcnd.main`'s pointer-condition schedule. Current lowering adds one explicit
byte-to-int promotion at logical instruction 581; the matcher now proves that
conversion and maps later fixed indices through it. Enabling the previously
dormant emitter exposed two duplicate-success-label defects in chained
word/long loop conditions. Separate body labels fix the assembler-invalid
output. The exact and one-constant near-match controls pass in both stack and
peephole modes, and stack/no-stack censuses show only `tptrcnd.main` changed:
35,074 fewer assembly-text bytes and 3,538 fewer instructions, with no
regressions. Checked execution improves peep cycles by 32.39% and nopeep cycles
by 34.19%, without moving the existing baseline.

The restored schedule executes 44 previously unexecuted included helpers.
Coverage reaches 4,197/4,385 functions, 170,069/190,675 lines,
86,483/144,952 native branch outcomes, and 152,851/170,503 regions. The raw
ledger has 58,175 uncovered outcomes, one reviewed and 58,174 unreviewed, plus
188 unexecuted included functions.

The pointer proof is additionally mutation-checked by changing every one of
the 81 active numeric comparison literals in `tptrcnd.main`; every variant
must compile, explicitly reject `pointer-condition-main`, and select a generic
emitter. This found 55 hardcoded semantic constants omitted by the original
matcher, all now part of the exact proof.

`tunion2.main`'s union-value schedule was two removed NOPs stale. Its logical
index adapter restores selection, while exact/near-match tests prove the
fourth local-name byte, aggregate make destination, `b = a` copy identities,
pointer-copy arguments, all six sum operands, and every field base used by the
two `b` reports. Stack/no-stack censuses show only `tptrcnd.main` and
`tunion2.main` changed. `tunion2` improves peep/nopeep cycles by 0.55%/0.49%
and sizes by 4.00%/3.92%, with no baseline changes.

Coverage is now 4,209/4,386 functions, 170,425/190,793 lines,
86,609/145,020 native branch outcomes, and 153,085/170,580 regions. The raw
ledger has 58,117 uncovered outcomes, one reviewed and 58,116 unreviewed, plus
177 unexecuted included functions.

`tbitfld.main`'s bitfield report schedule was four removed NOPs stale. A
bounded logical-view adapter reinserts only those no-op positions while
matching, invalidates definition caches around the temporary view, and restores
the physical MIR before emission. Exact and changed-aggregate-argument controls
pass in stack/no-stack, peep/nopeep, full-debug, and line-debug modes. Censuses
show only `tbitfld.main`, `tptrcnd.main`, and `tunion2.main` changed and no
regressions. `tbitfld` improves peep/nopeep cycles by 4.74%/5.62% and sizes by
28.95%/33.73%.

Coverage reaches 4,233/4,388 functions, 171,053/190,839 lines,
86,931/145,026 native branch outcomes, and 153,728/170,590 regions. The raw
ledger has 57,801 uncovered outcomes, one reviewed and 57,800 unreviewed, plus
155 unexecuted included functions.

`tclit.check_value_literals` was two removed instructions stale. Updating its
fixed call indices and complete semantic payload fingerprint restores the
exact value-literal schedule; the existing corrupted-call-ID control still
rejects it and executes generic code. It improves peep/nopeep cycles by
11.70%/12.15% and sizes by 7.35%/10.00%.

Coverage reaches 4,236/4,388 functions, 171,124/190,839 lines,
86,958/145,026 native branch outcomes, and 153,774/170,590 regions. The raw
ledger has 57,774 uncovered outcomes, one reviewed and 57,773 unreviewed, plus
152 unexecuted included functions.

Two-NOP and seven-NOP logical adapters restore `tstdlib.check_ldiv` and
`tclit.check_value_literals_extra`, with identity-removal and changed-pointer-
literal generic fallback controls. Historical focused fixtures preserve the
still-valid original final-call, argv traversal, and exec/execv workloads after
their main regression apps expanded. Added-call variants must reject each
named template and execute generically.

Exact controls now require an explicit accepted-template diagnostic from the
family dispatcher as well as final `scheduled-machine-cfg` selection. This
closes the former loophole where a different exact template could satisfy
`RequireExact`. Coverage reaches 4,262/4,392 functions,
172,203/190,939 lines, 87,522/145,040 native branch outcomes, and
154,845/170,612 regions. The raw ledger has 57,224 uncovered outcomes, one
reviewed and 57,223 unreviewed, plus 130 unexecuted included functions.

Coverage profiles now use LLVM's `%8m` online merge pool. Two independent full
runs produced byte-identical function summaries and gap ledgers; the former
`%p-%m` names could overwrite an earlier process when the OS reused a short-
lived compiler PID, making totals scheduling-dependent.

Historical focused fixtures execute the still-valid endgame boundary and errno
families with small runtime workloads. `tlimits.main`'s width schedule now
accepts a directly lowered unsigned-word addition as an explicitly proven
alternative to the old long-plus-cast shape. The diagnostic specialized
selector path now tries its narrow loop/comparison probes before universal
homed/spilled emitters; production ordering is unchanged. Five target loop
fixtures validate countdown, accumulation, unsigned division, repeated
invariant addition, and comparison selectors in all stack/peephole modes.

Mutation review found that the attempted struct-value logical adapter accepted
`proto_sum_pair(x)` where the exact emitter hardcoded `proto_sum_pair(y)`.
That adapter and its provisional coverage were removed rather than extending
an incomplete proof. The historical matcher remains in the denominator but
current lowering selects generic MIR for the 602-instruction shape. A separate
errno near match found that `close(98)` still emitted the schedule's hardcoded
99; its matcher now proves every corresponding bad-descriptor constant.

A newly reachable PHI-consumer forwarding test also exposed two real transform
defects: the pass read the PHI destination after clearing that instruction and
retained instruction pointers across insertion/reallocation. Capturing the
value IDs before mutation fixes both paths, and a ninth clean-build compiler
mutant is killed by the permanent post-transform assertions.

The corrected deterministic checkpoint is:

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,338 / 4,393 | 98.75% |
| Lines | 175,101 / 190,992 | 91.68% |
| Native branch outcomes | 88,685 / 145,086 | 61.13% |
| Regions | 157,056 / 170,662 | 92.03% |

The raw ledger has 56,132 uncovered outcomes, one reviewed and 56,131
unreviewed, plus 55 unexecuted included functions. The lower function
percentage than the provisional 99.27% report is intentional evidence that
unsafe exact-emitter execution was removed, not a denominator change.

The next generic-emitter checkpoint adds target-executed forced candidates for
two constant/dynamic inline byte-array stores and a regional adjacent-byte call.
The paired-byte near match inserts a field gap, must not contain the specialized
marker, and executes through a named generic selector in stack/no-stack and
peep/nopeep modes. Host controls add a successful spilled preflight followed by
an oversized-frame rejection and cover the dense-switch width query. The
wide-narrow multiply cache is verified on both `tlongopt` and the canonical
`tm1mu.mulmod` shape.

Review of that cache control found it rebuilt the cache before comparing,
making the diagnostic unable to detect a missed invalidation. It now compares
the preserved answer with the uncached proof whenever the generation is
unchanged, and rebuilds only for a new generation.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,344 / 4,393 | 98.88% |
| Lines | 175,275 / 190,997 | 91.77% |
| Native branch outcomes | 88,806 / 145,094 | 61.21% |
| Regions | 157,261 / 170,670 | 92.14% |

The raw ledger now has 56,019 uncovered outcomes, one reviewed and 56,018
unreviewed, plus 49 unexecuted functions. The two lazy-wide helpers are
structurally unreachable because lazy allocation admits only one- or two-byte
parameters while those helpers require four bytes. The remaining spilled
emitters are stale historical exact/inline shapes or a branch made dead by the
allocation optimization documented in its source; they remain in the
denominator.

The next exact-runner checkpoint restores two fully asserted semantic families.
A focused 6502 byte-math fixture exercises compare, decimal arithmetic,
OR/AND/XOR, ADC/SBC, and all negative/zero/carry effects; swapping the compare
arguments must reject the named template and execute generically. The abort
file runner accepts current lowering's 264-instruction form in addition to the
historical 269-instruction form. The only omitted instructions are the
post-`abort()` print and return that the compiler now removes after proving the
callee is `noreturn`; all pre-abort call, string, type, CFG, and observable
file/ctype behavior remains under the existing matcher proof. An added-call
variant rejects the exact template.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,358 / 4,393 | 99.20% |
| Lines | 176,268 / 191,011 | 92.28% |
| Native branch outcomes | 89,330 / 145,104 | 61.56% |
| Regions | 158,359 / 170,682 | 92.78% |

The raw ledger has 55,505 uncovered outcomes, one reviewed and 55,504
unreviewed, plus 35 unexecuted functions. Exact/near target controls, sanitizer
probes, both strict release gates, and stack/no-stack censuses pass.

The next checkpoint completes the existing lazy-wide implementation by
admitting nonaggregate four-byte parameters to the lazy allocation plan.
Target controls separately force a 32-bit parameter return and a 32-bit call
argument in stack/no-stack and peep/nopeep modes. Seven production apps adopt
the completed candidate, with no census removal or checked performance
regression.

The historical Fortran fatal schedule is covered by its ternary source form,
while the current qualifier-safe temporary spelling must execute generically.
Review-driven same-shape mutations also change `exit(1)` to `exit(2)`, stderr
to stdout, pointer subtraction order, and the lower range comparison. The
matcher now proves every emitted print argument, stream, global identity,
subtraction/division operand, range-bound computation, CFG/PHI relation, text
selection, and exit argument before accepting.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,361 / 4,393 | 99.27% |
| Lines | 176,575 / 191,181 | 92.36% |
| Native branch outcomes | 89,526 / 145,410 | 61.57% |
| Regions | 158,725 / 170,990 | 92.83% |

The raw ledger has 55,615 uncovered outcomes, one reviewed and 55,614
unreviewed, plus 32 unexecuted functions. Twenty-three are the intentionally
disabled struct-value exact schedule whose incomplete argument proof previously
accepted a known miscompile. The other nine are stale historical emitters or a
documented dead defensive path; all remain in the denominator.

After deletion was explicitly authorized when technically correct, the
remaining function gaps were resolved without adding exclusions:

- the historical forward-attention path and file-scope fixed-byte walk now
  have exact/generic target controls using real fixtures and source mutations;
- obsolete fixed-time checks (which assumed `time()` always returned -1), a
  preempted small-switch schedule, stale interpreter-only fusions, the stale
  constant-buffer schedule, and an impossible post-slot-allocation narrow
  forwarding branch were retired; and
- unrelated neighboring named-zero and word-load optimizations were retained
  and verified after deletion review caught an initially over-broad edit.

All nine historical owner apps passed full peep/nopeep execution with zero
checked regressions. Stack/no-stack selector censuses reported no changes.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,357 / 4,357 | 100.00% |
| Lines | 176,589 / 189,955 | 92.96% |
| Native branch outcomes | 89,591 / 144,652 | 61.94% |
| Regions | 158,744 / 170,023 | 93.37% |

Function coverage is complete. The broader objective is not: 13,366 lines,
55,061 native branch outcomes, 11,279 regions, and 54,814 raw unreviewed
outcomes remain.

The retained-code campaign begins with a target assignment matrix covering
long, float, plain-int, pointer, multidimensional, pointer-to-array, and struct
member forms. A host AST matrix independently covers malformed operators and
lvalues plus scalar, pointer, array, const, long, float, and every compound
operator. These tests add branch evidence without changing compiler behavior.

The struct-value schedule was subsequently retired rather than repaired. Its
dispatcher was still active and its historical matcher did not prove ordered
scalar/aggregate arguments; changing `proto_sum_pair(y)` to
`proto_sum_pair(x)` had demonstrated a real false acceptance. The exclusive
plan, matcher, emitter, and helper closure are removed. Generic spilled MIR
remains byte-for-byte, selector-for-selector, and cycle/size identical for
`tstructv` in both stack modes.

Permanent generic target controls cover the original aggregate workload,
swapped sum source, aggregate copy source/destination, first copy, peep/nopeep,
stack/no-stack, full debug, and line debug. The clobber execution manifest now
contains 576 unique target configurations.

| Metric | Before | After |
| --- | --- | --- |
| Functions | 4,361 / 4,393 | 4,361 / 4,369 |
| Lines | 176,575 / 191,181 | 176,545 / 190,652 |
| Native branch outcomes | 89,526 / 145,410 | 89,536 / 145,238 |
| Regions | 158,725 / 170,990 | 158,725 / 170,766 |

This is an actual source deletion, not a coverage exclusion. It removes 24
definitions, 529 lines, 172 branch outcomes, and 224 regions while preserving
every executed function. Eight retained functions remain unexecuted. The raw
ledger has 55,432 unreviewed outcomes.

The next retained-code checkpoint adds target-visible assignment coverage and
direct host AST support/rejection assertions. Full-corpus debug censuses then
found a real generic-emitter defect: a float multiply fused into `__fmaf` was
still classified as an independent wide helper handoff. Full debug first
rejected the resulting overlapping stack plan; suppressing only emission
exposed stale, unallocated slot loads and wrong runtime values. The final fix
rejects the fused multiply in the shared helper-consumer proof, keeping slot
planning and emission consistent. A permanent 12-configuration target runtime
matrix covers stack/no-stack, peep/nopeep, full debug, and line debug.

The coverage workflow now compiles all 482 runnable apps in each debug/stack
mode. It also normalizes a relative coverage build directory before exporting
`LLVM_PROFILE_FILE`; otherwise CTest writes the host-verifier profile below its
own working directory and silently drops host-only functions from the merged
report.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,357 / 4,357 | 100.00% |
| Lines | 176,746 / 189,957 | 93.05% |
| Native branch outcomes | 89,766 / 144,654 | 62.06% |
| Regions | 158,885 / 170,026 | 93.45% |

Against the preceding exact-function checkpoint this adds 156 covered lines,
142 covered branch outcomes, and 120 covered regions; the small denominator
increase is the new fused-multiply guard. The clobber manifest contains 616
executed configurations. Both strict 506-app release gates, all four 3,039
function debug censuses, sanitizer and debugger checks, nine compiler mutants,
and the frozen 482-app no-stack comparison pass with zero cycle/size
regressions.

A follow-up review proved that six shape-specific long/float assignment blocks
were structurally preempted by `ast_index_lvalue_elem_type`, which invokes the
same helpers and returns for every long or float element before those blocks.
The duplicate branches and one now-inert address computation are removed.
Direct host controls preserve long/float multidimensional acceptance and add
positive and negative pointer-array, dereferenced pointer-to-array, computed
pointer-expression, multidimensional-pointer, and member-pointer assertions.
Release and stack censuses remain byte-identical across all 3,039 functions.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,357 / 4,357 | 100.00% |
| Lines | 176,779 / 189,902 | 93.09% |
| Native branch outcomes | 89,810 / 144,500 | 62.15% |
| Regions | 158,891 / 169,853 | 93.55% |

This removes 55 lines, 154 branch outcomes, and 173 regions from retained
source. The direct pointer controls and a malformed-MIR preflight matrix add
covered outcomes for invalid return/value widths, unsupported opcodes,
unresolved memory, invalid indirect widths, direct/indirect call ABI failures,
aggregate-call ABI failures, and invalid `va_arg` offsets. Missing lines,
branches, and regions fall by 61, 184, and 163 respectively; the raw uncovered
ledger falls to 54,457 outcomes.

Six byte-math near-mutations independently alter the opcode mask, comparison
opcode, subtraction complement, addend order, overflow operand order, and
logical branch order. Each preserves its asserted target behavior, rejects the
named `byte-math-flags` schedule, and executes through generic MIR in
stack/no-stack and peep/nopeep modes. The expanded clobber manifest contains
640 configurations and covers four additional matcher outcomes, reducing the
raw ledger to 54,453.

Host controls also exercise exact-shape, selector-rejection, and backend-slot
diagnostic reporting on successful and malformed spilled candidates. These
supported diagnostic modes add 23 covered lines and 10 branch outcomes,
reducing the raw ledger to 54,443.

A diagnostic-only MIR mutator now changes one validated instruction field
during exact matching, then restores the complete instruction and invalidates
def-use caches before generic fallback. Checked parsing rejects malformed,
overflowing, out-of-range, or unknown mutations on both 32-bit-`long` and
64-bit-`long` hosts. The clobber harness clears inherited mutation settings
between cases and restores the caller environment only when the suite exits.
Thirteen `ln2` mutations cover every named log-series rejection group while
executing the original program generically; five source-level reorderings
provide independent controls. Both mutator functions have exact line, branch,
and region coverage, and the full clobber manifest contains 716 target
configurations.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,359 / 4,359 | 100.00% |
| Lines | 176,874 / 189,969 | 93.11% |
| Native branch outcomes | 89,869 / 144,548 | 62.17% |
| Regions | 158,961 / 169,908 | 93.56% |

The mutation framework adds two fully covered functions and reduces missing
lines, branch outcomes, and regions by 28, 11, and 15 respectively. The raw
ledger now contains 54,432 uncovered outcomes.

- Review the remaining 54,432 unreviewed raw uncovered outcomes rather than
  labeling them unreachable by default; add supported-input or malformed-IR
  assertions as needed.
- Preserve exact function coverage while closing the retained line, branch,
  and region gaps.
- Extend near-match/generic equivalence beyond the six enforced schedule families.
- Extend the seeded grammar beyond bounded unsigned arithmetic, conditional
  callbacks, and current memory/call forms.
- Add compiler mutants beyond the seven verifier/liveness controls and
  promotion-cache regression, and investigate survivors.

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
