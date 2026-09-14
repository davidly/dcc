# AST/MIR Correctness: Copilot CLI Handoff

Snapshot: 2026-09-14. This handoff requires no prior chat history, VS Code
session, local memory, or existing build artifacts. GitHub and the current
checkout are authoritative if the snapshot becomes stale.

## Current Continuation

PR #194 was merged into main as
`d49e3d7f50abc0432b25114719cd3c0252c546d6`. Its merge message records the
compiler fixes, proof controls, infrastructure changes, and remaining work.
The merge tree is identical to validated head
`5e32553b2d7f6cf5efe4833deb1ae02d860b9bf5`.

Continuation is on `test/ast-mir-proof-next`. The user now requires physical
removal of all legacy codegen emitters, not merely their exclusion from
coverage. Preserve active AST support and metadata helpers in mixed files,
all production generated-MIR emitters, and runtime/data/debug emission.
The initial deletion inventory is 115 classified legacy functions alongside
178 retained active functions; inspect their external callers and exclusive
dependencies rather than deleting mixed files wholesale.

After removal, run strict `runall.ps1 -Mode full -Extended` in stack and
no-stack modes first. Then run the complete aggregate correctness proof suite
and any maintained standalone proof missing from it on the same cleaned tree.
Both gates must pass before runner redesign or further proof implementation.
Fix failures rather than dropping controls or reusing pre-removal evidence.

The latest sealed pre-removal collection is
`build/mir-proof-suite-20260914-005110-3133574/compiler-coverage`, at the
validated head above. It contains 397 hashed raw profiles and 9,698 unique
clobber executions. Its scoped totals are 4,617/4,617 functions,
190,992/202,799 lines, 104,410/155,968 native branch outcomes, and
173,590/183,486 regions; 51,300 raw branch records remain unreviewed.
`inputs.json` SHA-256 is
`81da76ece50728aadadadaef399d51c6a18319b659a34380fe3e18c87d4fc6dd`.
Keep this evidence as the pre-removal baseline and collect fresh profiles after
cleanup. Source deletion and changes in denominators are not new test coverage.

**Removal completed.** All 115 manifest-classified legacy functions across
`dcc_ast_gen.c` (7), `dcc_ast_gen_cond.c` (27), `dcc_ast_gen_expr.c` (73), and
`dcc_ast_gen_support.c` (8) were deleted, along with their exclusive local
state and forward declarations. Two follow-on fixes were required and are
recorded here for the next reader:

- `dcc_decl.c`'s bitfield-initializer parser still called the now-removed
  `ast_gen_expr()` inside a dead `!mir_is_active()` fallback branch. It now
  calls `mir_capture_bitfield_init_expr()` unconditionally, matching every
  other initializer path in that file. The now-orphaned
  `emit_store_bitfield_from_hl()` helper (`dcc_expr.c`/`dcc.h`) was removed too.
- A header-cleanup script transiently deleted the unrelated declaration of
  `ast_stmt_supported()` from `dcc_ast.h` as collateral damage (a trailing
  comment after a semicolon defeated the "end of declaration" heuristic for
  the neighboring `ast_gen_expr()` removal). Restored it and refreshed the
  stale "AST codegen is the compiler's only codegen path" comment above it.
- `scripts/ast-function-coverage.json` needed its 115 `"legacy"` entries and
  7 `guarded_edges` cleared; the manifest's static validator otherwise reports
  them as stale (classified but no longer present in source). All 178
  production functions remain classified and unchanged.

`scripts/build-dcc.ps1` does **not** track header dependencies for incremental
compilation: editing a header without touching dependent `.c` files can leave
a stale `.o` silently linked. A full `rm -rf build/dcc build/dccpeep
build/dccrtlstrip build/dccmake build/m80c build/l80c` before rebuilding is
required after any header-only change during cleanup work like this.

Both mandatory user-ordered gates passed on a genuinely clean rebuild:
strict `runall.ps1 -Mode full -Extended` in stack and no-stack modes (482
passed, 24 documented skips, zero failures, zero performance regressions
each), and the complete `run-mir-proof-suite.ps1` 11-phase suite (canonical
and independent builds, 134 script tests, normal and ASan/UBSan host suites
5/5 each, debugger-host tests, one passing baseline plus 24/24 killed
compiler mutants, both strict release gates, all four 3,039-function debug
censuses, and a sealed coverage collection with 9,698 unique clobber leaves).
Parent and stack/no-stack censuses before and after removal are byte-identical
across all 3,039 functions: this was a pure dead-code deletion.

The fresh post-removal coverage checkpoint is
`build/legacy-removal-proof-suite/compiler-coverage`; its `inputs.json`
SHA-256 is `78c9ddac16468a1d53b065129d9e88e58e248e538e0447fd9d023f5f62f328ab`.
Its scoped totals are 4,617/4,617 functions, 190,968/202,741 lines (94.19%),
104,401/155,940 native branch outcomes (66.95%), and 173,573/183,450 regions
(94.62%); 51,281 raw branch records remain unreviewed. Relative to the
pre-removal collection, the function-scoped denominator shrank by 58 lines,
28 branch outcomes, and 36 regions, entirely from deleting the now-unreachable
`!mir_is_active()` fallback branches inside five still-active production
functions (`ast_emit_init_expr`, `ast_emit_discarded_expr`,
`ast_emit_struct_init_expr_assign`, `prepare_inline_arg_temps`,
`prepare_inline_local_temp`). This is a justified denominator reduction from
deleting genuinely dead code, not a new executed-coverage claim.

This removal work is committed and pushed without waiting for GitHub Actions,
per the user's local-validation policy. Continue with runner-inventory
hardening and the ranked correctness waves next; the broad 100%
correctness-coverage objective remains incomplete.

The sections below retain historical checkpoints. Instructions to leave legacy
emitters for future removal, wait for GitHub Actions, or continue from an older
branch are superseded: remove the legacy emitters now, fully validate locally,
push, and do not wait for Actions. The broad 100% correctness-coverage objective
remains incomplete.

## Mission

Strengthen confidence in dcc's production AST/MIR pipeline with meaningful,
assertion-backed tests, independently reproduced compiler fixes, and honest
coverage accounting. The purpose is correctness across supported C constructs
and Z80 contracts, not merely increasing a coverage percentage.

The original request was to fix a generic MinMax failure independently of an
exact machine schedule that concealed it, integrate stacked PRs, and validate
cross-platform. That work is merged. The subsequent, broader request is to:

- Close meaningful supported-input and malformed-IR coverage gaps.
- Expand CFG, dominance, PHI, call ABI, aliasing, clobber, spill, and cache tests.
- Test exact-selector rejection and correct generic generated fallback.
- Extend target-aware differential generation and compiler mutation tests.
- Review uncovered/excluded code with evidence, without manipulating totals.

The broader request is NOT complete. Function coverage is exact, but retained
line, branch, and region gaps plus tens of thousands of raw branch records
still need investigation.

## Parallel Execution and Test Cadence

The current workflow replaces sequential per-family full-suite runs with
isolated background workers and consolidated integration gates:

- `pwsh scripts/run-mir-proof-suite.ps1` is the phase-gated aggregate entry point
  for all maintained proof layers. It includes canonical and independent
  builds, script/static audits, normal and sanitized host tests, debugger-host
  tests, isolated compiler mutants, both strict release modes, the extended MIR
  census, and the instrumented coverage workflow. The concurrent preparation
  gates also include the standalone `test-mir-fuzz-source.ps1` generator proof.
  `-List` prints its ordered gates without executing them, `-All` is an
  explicit no-op alias for "run everything", and `-RequireComplete` forwards
  `DCC_COVERAGE_REQUIRE_COMPLETE=1` into the final coverage gate. Successful
  runs also emit `<output>/receipt.json` with the effective parameters and
  per-phase timestamps. Independent preparation gates run concurrently, the
  stack/no-stack release gates split the CPU budget, and mutation/coverage
  phases retain their existing bounded schedulers.
- GitHub CI intentionally runs only the standard cross-platform
  `runall.ps1 -Mode full` regression gate. Developers run the aggregate proof
  suite periodically after fully validating proof-related increments locally;
  clobber, mutation, coverage, extended-corpus, sanitizer, and debugger proof
  gates are not duplicated on every push or pull request.
- Assign disjoint source modules and data-only case manifests to workers.
  Keep one owner for shared host tests and one integrator for publication.
- Workers run new/changed cases and valid controls, not the entire corpus.
  Test-only changes reuse unchanged native release/debug evidence.
- Integrate ready worker changes into an immutable checkpoint, then run the
  required full gates once for that combined tree before publishing production
  changes. Run the full clobber corpus inside coverage, not again immediately
  before the same coverage workload.
- Use bounded child processes with `run-mir-clobber-tests.ps1 -Jobs N`.
  `-ListExecutions PATH` enumerates exact leaf keys; `-ShardIndex` and
  `-ShardCount` allow explicit partitions. The merged successful manifest must
  equal the expected inventory exactly, without missing or duplicated leaves.
- New independent campaigns belong in `scripts/mir-clobber-cases/*.json`.
  Their explicit `Group` aliases avoid collisions with existing case names.
- `run-mir-compiler-mutations.ps1 -Jobs 2 -BuildJobs 2` runs an unmutated
  baseline first, then isolated clean mutant builds; crashes and build failures
  are invalid results, not mutation kills.
- Use `DCC_COVERAGE_STAGE=build|collect|report` to stage an immutable coverage
  checkpoint. `DCC_COVERAGE_JOBS` defaults to all online CPUs for builds,
  runall, and host CTest; mutation and census concurrency can be overridden
  with `DCC_COVERAGE_MUTATION_JOBS` and `DCC_COVERAGE_CENSUS_JOBS`. The
  diagnostic-heavy clobber runner defaults to the measured optimum of eight
  workers and can be tuned with `DCC_COVERAGE_CLOBBER_JOBS`. Exhaustive
  mutation campaigns use a longest-first token scheduler: ordinary campaigns
  default to four workers, enforced caps remain at two, and completed campaigns
  immediately release capacity within the combined mutation budget. Separate
  `%8m` profile pools avoid serialization on profile-file locks.
  Report-only runs verify recorded input/tool/profile hashes and do not rerun
  targets. Do not merge worker revisions or faulty compiler profiles.

The latest local aggregate validation used 24 workers. Phases 1-10 passed under
`build/mir-proof-suite-validation-4`: 131 script tests, all static/runtime
audits, normal and ASan/UBSan MIR host tests (5/5 each), debugger-host tests
(10/10), 23 killed compiler mutants plus the valid baseline, both strict
stack/no-stack release gates, and 548 extended-census rows across both modes.
Phase 11 separately ran four 3,039-function debug censuses. Its
phase-11 publication correctly stopped because `scripts/runall.ps1` changed
after the immutable input snapshot. A clean phase-11 replay against the
unchanged current tree then passed under
`build/mir-proof-suite-validation-5/coverage`, including all clobber and
mutation campaigns, 5/5 instrumented host tests, 397 hashed raw profiles, and
final checkpoint verification. Runner and process-supervision hardening then
made that snapshot stale. The exact hardened tree was recollected under
`build/mir-proof-suite-validation-6/coverage`; all 40 mutation campaigns, four
3,039-function debug censuses, 5/5 instrumented host tests, and final immutable
checkpoint verification passed again with 397 hashed raw profiles. The
subsequent review found and fixed three runner-isolation defects: concurrent
release diagnostics shared one directory, Windows-native coverage paths were
passed directly to POSIX `sh`, and lowercase `dcc_*` controls escaped
sanitation. The complete 134-test script suite and a focused isolated
116-diagnostic run passed after those fixes. Validation 6 was correctly
rejected as stale; the exact final tree was recollected under
`build/mir-proof-suite-validation-7/coverage`. All 40 mutation campaigns, four
3,039-function debug censuses, 5/5 instrumented host tests, all clobber
campaigns, 397 hashed raw profiles, and final immutable checkpoint verification
passed. Its `inputs.json` SHA-256 is
`567e9ba8fabdcc8b76865872255406117e35a4b7f0b79ccb2e0ea3eda5e7c2b1`.
The
selected AST/MIR result remains 4,617/4,617 functions (100.00%),
190,992/202,799 lines (94.18%),
104,409/155,968 branches (66.94%), and 173,590/183,486 regions (94.61%), with
51,301 branch outcomes still unreviewed. These generated artifacts are local
evidence, not tracked files or a claim that broader correctness work is done.
The subsequent paired-byte increment adds four forced-regional field-gap
controls. A second CFG near match executes through a named generic emitter and
requires an exact `read_pair` rejection when `regional` is forced. All 20
paired-byte stack/no-stack and peep/nopeep configurations and
the 134-test script suite pass; the frozen built-in clobber inventory is now
5,076 leaves. A dedicated compile probe also requires the adjacent control to
emit the paired-byte marker and the field-gap form to omit it. A clean-build
mutant that disables only the nonadjacent-offset guard is killed by that exact
assertion; the complete mutation campaign now has one passing baseline and
24/24 killed mutants. A subsequent independent-dominance control directly
rejects a PHI with no incoming edge for its first logical predecessor. Focused
LLVM 18 coverage changes that branch from 0 to 1 false outcome; normal and
ASan/UBSan host tests, all 134 script tests, and all 24 mutants pass.

Each worktree needs its own binaries and CMake output directory. The canonical
build's `-OutputPath` redirects intermediate artifacts, not repository-root
tools; it is not sufficient isolation for simultaneous builds in one checkout.
Use a combined CPU budget across workers and nested build/test jobs.

The legacy AST/body emitters remain excluded; active metadata and MIR support
remain in scope. The final 100% claim still requires two clean collections with
exact equality for all four scoped metrics, plus correctness and unchanged
performance baselines. Parallel execution and fewer repeated gates do not
relax those completion criteria.

The first integrated parallel wave retains all 5,052 allocation-checkpoint
configurations and adds 24 scanner controls, for exactly 5,076 successful
executions. Its generic `va_arg` offset fix rejects `LONG_MAX` without overflow,
partial text, or consumed labels; same-stream recovery matches a clean valid
candidate. Strict release gates, frozen performance comparison, sanitizer,
debugger and all nine clean compiler-mutant controls passed.

The next two integrated waves raise the exact execution inventory to 5,196
unique configurations. They add accepted and generic-rejection proofs for
symbol-find, softmax, matrix-product-add, and directory-enumeration schedules;
make MIR stream seeks overflow-safe, bounded, and transactional; and add two
target-aware allocation matcher compiler mutants. The combined tree passed
both strict 506-app release modes, canonical and independent builds, an exact
stack/no-stack frozen-parent census comparison, ASan/UBSan host and compiler
probes, 10 debugger-host and two line-debug tests, all 11 compiler mutants,
120 repository script tests, and the full instrumented collection.

The next wave raises the exact inventory to 5,292 unique configurations with
matrix-product-store, symbol-insert, and compound-runner proofs. Review also
found that nameless direct and aggregate MIR calls could reach spilled
emission, produce invalid call targets, and perturb retry state. Both forms
now reject during side-effect-free preflight; valid direct and indirect
controls plus empty-output, label-rollback, and byte-identical retry assertions
pass under normal and ASan/UBSan host builds.

The following wave raises the exact inventory to 5,420 configurations and
finds four additional correctness gaps: late homed parameter rejection
perturbed retry state, the aliased byte-sum schedule no longer matched current
MIR, constant do-while loads and unary byte-sum conversions were under-proven,
and active integer folding accepted non-integer operands and failed to
normalize `_Bool`. The fixes retain generic fallbacks and improve `tbcregno`
cycles by 15–20% without moving its baseline. Static classification validates
287 mixed-AST functions and no new test executes the excluded strict-fold or
legacy emitter path.

The next wave raises the exact inventory to 5,548 configurations with LCS and
packed-record proofs plus defined float-to-integer and float-to-`_Bool`
dereference assignments. Review caught and fixed a packed fastcall ABI hole
and removed an undefined signed-char conversion oracle. Scalar-DAG preflight
now proves the complete value graph before producing output, externals, or
labels, so repaired retries remain byte-identical.

The following wave raises the exact inventory to 5,648 configurations with
new sliding-maximum and ctype/realloc ABI proofs. It also preflights homed
scalar DAGs before stack-check, external, or label side effects and fixes
deferred MIR insertion so declaration placeholders and exclusive scope ends
move with debug points. Coverage provenance now includes both host-test
binaries, and compiler-mutant workspaces copy all host C test sources. An
initial collection was correctly rejected after a concurrent script test
changed `dccmake`; the clean rerun used an immutable tool bundle.

The next wave raises the exact inventory to 5,856 configurations with
multidimensional-array and recursive MinMax proof hardening. Malformed virtual
operands now reject before spill-slot interval indexing, and declined selector
attempts restore label state before fallback. An initial selector host-test
integration compiled the selector as a second translation unit and duplicated
68 maintained functions in the report; that report was discarded. The test
now links the normal selector object, and LLVM 18 confirms zero duplicate or
misattributed selector functions without any exclusion.

The following wave raises the exact inventory to 6,018 configurations. It
isolates the affine selector fallback, canonicalizes scalar-DAG cast emission,
and adds a 2,048-case byte-math oracle plus fixture-backed directory ownership
and failure controls. Review added direct-call proofs for byte-math helpers and
validates every fixed and variadic directory-call argument type before exact
emission.

The next wave raises the exact inventory to 6,224 configurations with numeric
bitfield assignments, packed-record ABI proofs, and opcode-aware homed operand
validation. Review found a high-severity fixed-softmax false acceptance:
changing `sum += *item` to subtraction still selected code that added. A
complete fixed-kernel proof and 206-mutation survivor audit now cover operators,
dataflow, types, table bounds, call ABI, loops, and normalization with a defined
upper-clamp sentinel oracle.

The following wave raises the exact inventory to 6,320 configurations with
numeric structure-member assignments and square-grid/endgame-scope proofs.
Spilled MIR now validates opcode-required operands before allocation indexing,
closing 38 sanitizer-reproduced malformed-input paths while preserving valid
void-call and void-return sentinels. Endgame tests were moved from a private
runner into the shared coverage inventory.

The next wave raises the exact inventory to 6,528 configurations with
additive-subscript, Fortran-fatal, and long-index proof campaigns. Missing and
unresolved branch targets now reject before spilled frame planning. Fortran
fatal emission now uses canonical assembler names for static or mangled print
and exit callees; long-index tests were migrated from a private script into the
shared coverage inventory.

The following wave raises the exact inventory to 6,602 configurations with
homed branch-target validation, pointer compound assignments, abort-file
proofs, and constant-function evaluator controls. A blanket signed-overflow
rejection caused a reproduced 4.4x `tregnarw` regression and was rejected;
DCC's established target-width wrap semantics were restored, returning `lbig`
to its parent 18-byte, 2-instruction exact schedule and zero checked
performance regressions.

The next wave raises the exact inventory to 6,758 configurations. AST-to-MIR
call lowering now rejects malformed direct and indirect callee chains before
side effects, comparison selection validates parameter displacements, and the
exec-recursion schedule proves operators, volatility, ABI, and complete
dataflow. A 127-mutation exec audit has zero survivors. Coverage reporting now
includes the already instrumented scalar-DAG host binary in every
`llvm-cov` report, export, and show operation rather than merely hashing it in
provenance.

The following wave raises the exact inventory to 6,854 configurations. It
supports live results from word-sized multidimensional compound assignments,
hardens the compound-check schedule's ABI, memory, control-flow, and promoted
assignment proofs, and completes the repeated-invariant-add legality proof.
Independent review found that `_Bool` loop counters were incorrectly accepted;
the exact schedule now rejects every boolean counter type surface. The repeated
add audit has zero survivors across 34 mutations. The compound audit rejects
1,482/1,575 mutations; all 93 survivors are evidenced as 72 field-identity
rewrites, 10 byte-identical stores, and 11 overwritten-before-read stores with
passing runtime oracles.

The next integrated waves raise the exact inventory to 7,436 configurations.
They harden deferred metadata repair, spilled/homed CFG preflight, expression
lowering, scalar DAGs, comparison branches, constant evaluation, VLA
smoothing, affine fill, scope/endgame, call-safe member sums, variable
softmax, byte math, multidimensional arrays, and both retained directory
layouts. Reviews found and fixed malformed CFG allocation hazards, call
dominance/result ownership gaps, cyclic AST traversal, valid-type
over-rejection, indirect-call acceptance, and several performance regressions.
Exhaustive committed campaigns now cover more than 25,000 semantic mutations.

Waves 20-27 raise the locally verified exact inventory to 9,336 unique
configurations. They extend exact and generic proofs across long-index,
packed/flagged records, file and directory I/O, matrix and softmax kernels,
symbol operations, pointer conditions, casts and promotions, nontrivial
loops, recursion, abort handling, callback registration, memory exercise, and
buffered console I/O. Reviews found and fixed additional fastcall ABI false
acceptance, signedness and cached-state errors, invalid string targets,
recursive matcher hangs, and incomplete value/CFG/type proofs. Unsupported
forms retain generated homed/spilled fallback.

Wave 28 raises the exact inventory to 9,564 unique configurations. It hardens
allocation lifetime, byte-equality, and nonlocal `_setjmp`/`_longjmp` exact
schedules with complete type, width, storage, volatility, alias, CFG, PHI,
dataflow, call-ID, prototype, ABI, return, stack, and debug proofs. The three
audits exercise 9,216 mutations with zero meaningful survivors; supported
forms retain exact output and unsupported forms retain generated
homed/spilled fallback.

Waves 36-39 retain the locally verified exact inventory at 9,686 configurations.
Wave 32 added a combined PHI, spill, alias, call-clobber, and post-call reload
proof; Wave 33 preserves compatible callable prototypes through conditional
expressions and MIR PHIs; Wave 34 snapshots resolved scalar-call signatures by
call ID; Waves 35-36 prove that narrow and wide values live across ordinary
calls receive only safe homes; and Waves 37-38 prove the guarded narrow and
wide late-PHI exceptions preserve their caller-saved homes.
The authoritative Wave 30 LLVM checkpoint remains 4,608/4,608 functions
(100.00%), 190,711/202,512 lines (94.17%), 104,229/155,732 native branch
outcomes (66.93%), and 173,307/183,178 regions (94.61%). Its raw uncovered
ledger is 51,245. The later historical sections retain earlier checkpoints;
use Wave 30 for current full-collection metrics and Wave 32 for the current
locally verified execution inventory.

## Publication State

- Repository: <https://github.com/davidly/dcc>.
- Continuation branch: `test/ast-mir-correctness`.
- Continuation PR: <https://github.com/davidly/dcc/pull/194>.
- PR #193 was merged as
  `74079b980a282e966b99d878256f89f799b63a64` on 2026-09-08.
- Latest locally validated continuation implementation:
  `465ce55f` (`Prove wide call-crossing spills`).
- Parallel-wave implementation checkpoints:
  - `3c85d83d` — allocation-lifetime matcher coverage;
  - `3d109f26` — accepted/rejected sliding-maximum controls;
  - `ac0cb97c` — overflow-safe generic `va_arg` offset preflight;
  - `37c2dc4d` / `83805f4f` — exact, profile-safe clobber sharding;
  - `b384aa19` — isolated bounded compiler-mutant workers; and
  - `d6ad345c` through `568302dd` — bounded shared process-tree supervision;
  - `aa2e1bb9` — transactional bounded MIR stream seeking;
  - `20afc983` / `d9052b2f` — symbol-find and softmax matcher proofs;
  - `15e1807c` — target-aware allocation compiler mutants; and
  - `41c7c62e` / `e583976a` — matrix-add and directory-enumeration proofs;
  - `2964a774` / `08b7e938` / `149dcabb` — matrix-store, symbol-insert,
    and compound-runner proofs; and
  - `4661c228` / `3ca20abb` — transactional direct and aggregate call
    preflight;
  - `34dc1706` / `d1fba9e7` — hardened constant do-while proofs;
  - `7a710e7f` / `2b5993b9` — transactional homed parameter preflight;
  - `4ac605f3` / `9e2e7d84` — aliased/direct byte-sum proof restoration; and
  - `059d6fee` through `6be3462c` — active-only integer and `_Bool` fold
    correctness;
  - `e9317bcf` — LCS exact proofs;
  - `07512363` / `38c9b425` — defined float pointer assignment support;
  - `b06b122b` / `2d84f13f` — packed-record proof and fastcall ABI hardening;
    and
  - `b4776307` — transactional scalar-DAG preflight;
  - `1021efdf` — transactional homed scalar-DAG preflight;
  - `d411dd2a` / `4a85836c` — sliding-maximum and ctype/realloc ABI proofs;
  - `69c94414` — deferred declaration/scope metadata repair; and
  - `e41dea92` / `893bc5bd` — complete host-test provenance and mutation
    workspace inputs;
  - `b62bdb3a` / `471de8da` — transactional selector labels and
    single-translation-unit coverage;
  - `0710c912` — spill-slot operand bounds;
  - `1d29427f` / `0f0a5537` — multidimensional array proofs; and
  - `5cd36798` through `b6daaa16` — recursive MinMax proofs and standard
    campaign integration;
  - `5044557a` / `aab56e4a` — affine fallback transaction and linked host
    access;
  - `45dd8b14` — scalar-DAG cast and preflight correctness;
  - `49d3da4a` / `4425846c` — byte-math proof and direct-call hardening; and
  - `ce71729b` / `fee9f2a3` — directory dataflow, ownership, and argument ABI
    proofs;
  - `98e8d7fb` / `36232484` — complete homed operand preflight;
  - `e17dfb9f` — numeric integer bitfield assignments;
  - `00554317` — additional packed-record ABI proofs; and
  - `1c8c01b0` through `9a92aa0a` — complete variable and fixed softmax
    semantic proofs;
  - `91050259` — opcode-aware spilled operand preflight;
  - `acc02c27` / `0fedef02` — numeric structure-member assignments;
  - `cd9cd9c1` — square-grid proof controls;
  - `930bf5b0` / `3eab3f44` — endgame-scope proof and shared campaign; and
  - `b847b62e` — CI now builds every registered MIR host-test target before
    CTest on Linux, macOS, and Windows;
  - `2b8ab7bb` — transactional spilled branch-target preflight;
  - `af620931` — additive-subscript proof controls;
  - `9beb9817` / `1497ca3d` — Fortran-fatal proofs and symbol-aware calls; and
  - `8aabd5bd` / `acac351c` — long-index proof and shared campaign;
  - `7a073161` — transactional homed branch-target validation;
  - `a4666f1c` / `12975ddc` — pointer compound assignment support;
  - `13e979f0` — abort-file runner proofs; and
  - `be9ac5c4` / `c16b8010` — constant evaluator correctness with preserved
    target wrap semantics and performance;
  - `08409d8b` / `71081ed8` — transactional direct and indirect AST-call
    lowering preflight;
  - `527fde43` — comparison parameter-displacement validation;
  - `d37c8f81` through `27e80ada` — complete exec-recursion operator,
    volatility, ABI, and dataflow proofs; and
  - `3d306673` — include the scalar-DAG host binary in all LLVM coverage
    reports;
  - `1b909d5f` — live multidimensional compound-assignment results;
  - `a63128dc` — compound-runner structural and ABI hardening;
  - `cfdbecd1` / `c1d6e591` — repeated-invariant-add proof hardening and
    boolean-counter rejection; and
  - `a3cab0df` — updated exact built-in inventory contract;
  - `a33e0856` — nonlocal exact schedule and ABI hardening;
  - `4356ba24` — byte-equality exact-runner proof hardening; and
  - `1c15706c` — allocation-lifetime ownership and error-path proof hardening.
- Wave 14 passed both strict 506-application release modes with no regressions,
  an independent release build, normal and ASan/UBSan host CTests, selector
  isolation, 11 compiler mutants, 10 debugger-host tests, two line-debug
  tests, and exact stack/no-stack frozen-parent census comparisons.
- Its immutable LLVM 18 collection contains 43 profile-pool files and exactly
  6,758 unique clobber executions. The manifest SHA-256 is
  `eb7215b69af536402d4f09b358f3686329bfadaec7eb103b77ab832b206153c0`;
  `collection.json` records the same digest.
- Wave 15 passed both strict 506-application release modes with zero
  regressions, normal and ASan/UBSan host CTests, 152 focused target
  configurations, the eight-probe candidate matrix, required-emission checks,
  11 compiler mutants, 10 debugger-host and two line-debug tests, all 120
  repository script tests, and exact 3,039-function stack/no-stack
  frozen-parent censuses.
- Its immutable LLVM 18 collection contains 43 profile-pool files and exactly
  6,854 unique clobber executions. The manifest SHA-256 is
  `d590eac25af8174ee7ec4f271fc51ee011db3c03c9fb0a4689f0254f7245ed96`;
  `collection.json` records the same digest.
- The Wave 16-19 implementation spans `0fbcf2c3` through `3fc259c8`;
  `b08957cd` parallelizes coverage and removes duplicate VLA host mappings.
  The final tree passed both strict release modes with zero regressions,
  five normal and sanitized host CTests, 11 compiler mutants, debugger tests,
  120 script tests, exact frozen-parent censuses, and all focused campaigns.
- The corrected LLVM 18 collection at `b08957cd` contains 85 profile-pool
  files and exactly 7,436 unique clobber executions. Its manifest SHA-256 is
  `afa127b37d9096d1a4e5233d81a0c540b04894638e1c3353c8fbeed8120e149a`.
  Parallel mutation campaigns with distinct profile pools reduced collection
  time from about 116 minutes to about 67 minutes on this 24-CPU host.
- Waves 20-27 are published through `708f5b14`. The final Wave 27 tree passed
  both strict 506-application release modes with zero regressions, five normal
  and five ASan/UBSan host tests, 262 focused target configurations, candidate
  and required-emission controls, all 120 repository script tests, and an
  exact 9,336-leaf inventory with SHA-256
  `44c207fcb6bdc47ffb1d121e160d3d089749f06ca91640beede20075de05021d`.
  The most recent authoritative full LLVM collection remains the duplicate-free
  Wave 19 collection above; no later failed or partial profiles are reused.
- Wave 28 is locally validated through `1c15706c`. Its three semantic audits
  exercise 9,216 mutations with zero meaningful survivors, and its 9,564-leaf
  execution inventory has SHA-256
  `89c8702842240ff322a8948a84314ccdf17841de3cc97fbadc1be3d837bfbfad`.
  The integrated tree passed both strict 506-application release modes with
  zero regressions, canonical and independent builds, normal and sanitized
  focused audits, all 120 script tests, all 11 compiler mutation controls,
  all 10 debugger-host tests, runtime IY/coverage audits, and module export
  audits. This inventory does not replace the authoritative Wave 19 full LLVM
  collection; a fresh immutable full collection remains the next coverage
  measurement.
- The first immutable Wave 28 attempt at `c924ea08` exercised all 40 mutation
  campaigns but was not sealed. After 4 hours 8 minutes it correctly rejected
  a stale pointer-condition audit expectation: all 14,706 mutations completed,
  but 246 label `immediate` mutations were classified as unused opcode fields
  rather than no-ops. MIR labels use their `label` member, so commit `2f657c2a`
  corrects the expectation and adds a focused regression. No profiles from the
  failed collection are authoritative or reused.
- A post-run clobber benchmark found eight workers fastest: the same 476
  `allocmut` configurations took 16.90, 14.51, 16.85, and 17.99 seconds at
  4, 8, 12, and 16 workers, with identical manifests. The next collection uses
  eight clobber workers and a global mutation token scheduler. A real
  four-worker scheduler smoke completed all 1,692 matrix-add mutations in
  20.44 seconds with zero meaningful survivors. Commit `4930df94` publishes
  the scheduler and measured default.
- Wave 29 begins with `6aa1f447`. The independent dominance verifier now
  rejects PHIs whose declared labels do not correspond to real incoming CFG
  edges, even when the missing edge is unreachable. The proof preserves
  unreachable real predecessors, multiple physical arcs carrying one logical
  input, repeated arcs from one predecessor, and arbitrary consecutive entry
  label aliases. The original malformed diamond was reproduced before the
  fix. Validation passed identical 3,039/3,039 stack and no-stack censuses,
  both strict 506-application release modes with zero regressions, five normal
  and five ASan/UBSan host tests, 11 compiler mutants, 392 strict extended
  configurations, required-emission and runtime audits, all 128 script tests,
  and all 10 debugger-host tests. The change only rejects malformed internal
  MIR and does not alter debug metadata or valid generated output.
- The first immutable Wave 29 collection completed every mutation campaign,
  then stopped honestly at the first strict full-debug census: `tarray` could
  not emit `ShowBinaryData`. This pre-existing defect was reproduced at the
  pre-PHI `4930df94` compiler. An attempted emitter-side relaxation compiled
  but produced an empty CP/M hexdump and was discarded. Commit `e33bfb34`
  instead repairs the source invariant: when full `-g` metadata restores a
  named load's declared type, deferred metadata also reconstructs the erased
  call-argument and wide-comparison conversions. Direct calls are fully
  prevalidated before insertion, so late, duplicate, or sparse arguments
  remain transactional. Focused `tarray` full-debug and stack-debug binaries
  match the checked baseline; all four 3,039-function debug censuses emit MIR;
  optimized stack/no-stack censuses remain byte-identical; both strict
  506-application release modes, five ASan/UBSan compiler tests, 11 compiler
  mutants, 392 extended configurations, 128 script tests, and all 10 debugger
  tests pass. The failed collection remains raw evidence only; restart from
  this new commit for the next authoritative ledger.
- The immutable replacement Wave 30 collection is sealed at exact revision
  `864573c38db01a01f2b944d77a26f9132cfaacd2`. Report-only regeneration
  revalidated every recorded input, tool, binary, profile, and execution
  manifest hash. Its 397 non-empty profile-pool files contain exactly 9,564
  unique clobber executions; the manifest SHA-256 is
  `89c8702842240ff322a8948a84314ccdf17841de3cc97fbadc1be3d837bfbfad`.
  The authoritative function-scoped result is 4,608/4,608 functions
  (100.00%), 190,711/202,512 lines (94.17%), 104,229/155,732 native branch
  outcomes (66.93%), and 173,307/183,178 regions (94.61%). The raw ledger has
  51,245 unreviewed branch outcomes and no unexecuted maintained functions.
  All collection workloads passed, including both release modes, all four
  3,039-function debug censuses, the mutation campaigns, and five host CTests.
  This supersedes Wave 19 as the coverage ledger, but it does not complete the
  broader justified-coverage objective. The recent deferred-metadata resolver
  is the first evidence-backed review target: it has 60 uncovered regions and
  182 uncovered branch outcomes around call and conversion repair.
- The first Wave 31 increment adds permanent host invariants for repeated call
  IDs, negative positions, fixed and variadic arity, sourced and non-function
  direct calls, release-mode gating, right-hand wide-comparison conversion,
  and already-sourced function-pointer calls. Two clean-build mutants remove
  repeated-ID rejection and the full-debug comparison gate; both are killed,
  taking the compiler mutation suite from 11 to 13 killed mutants. A focused
  profile overlay on sealed Wave 30 data adds 15 covered `dcc_mir.c` lines,
  13 branch outcomes, and three regions with unchanged production-source
  denominators. The shared call validator moves from 40/50 to 44/50 branch
  outcomes in the first increment and then 48/50 after negative-ID,
  prototype-bound, unprototyped, and reverse-position controls. Its only
  remaining outcomes are defensive `call_index` bounds that the private
  in-range loop caller cannot violate. The function-pointer wrapper moves from
  5/10 to 6/10 and the resolver from 698/880 to 702/880. This overlay is
  prioritization evidence, not a new authoritative full checkpoint. All five
  normal and sanitized host CTests and all 128 script tests pass; production
  code and release output are unchanged.
- The first Wave 32 increment adds a target-aware interaction proof that was
  absent from the separate alias and PHI controls. Both an unsigned-byte and
  unsigned-word function merge a branch-selected value through a real
  two-input PHI, keep four derived values live across an aliasing call, and
  consume both the call result and a post-call reload. Each function has four
  blocks, maximum liveness seven, three spills, four cross-call values, two PHI
  moves, and selects `spilled-scalar-cfg`. Eight hardcoded target-width oracles
  cover both PHI arms, aliasing and disjoint pointers, and byte/word wrap
  boundaries; an independent Python oracle recomputes every result and the
  final hash. A deliberate result-bit fault produces all eight named failures,
  proving that the runtime discriminator is active. The campaign adds 26
  stack/no-stack, peep/nopeep, full-debug, and line-debug configurations,
  raising the exact inventory from 9,564 to 9,590. All 26 configurations and
  all 129 repository script tests pass, and LLVM 18 accepts the fixture as
  strict C89. An isolated 27-profile current-tree collection adds no outcomes
  when overlaid on the unchanged-function Wave 30 evidence. This is therefore
  a cross-contract semantic proof, not a raw percentage increase; the sealed
  Wave 30 totals remain authoritative. Production code, release output,
  coverage denominators, and performance baselines are unchanged. Commit
  `ff638de3` publishes the fixture, campaign, and independent oracle.
- Wave 33 reproduces and fixes two callable-signature failures. First, MIR
  verification recovered prototypes only from directly named loads or
  parameters, so a PHI of two identical callbacks accepted excess arguments
  and wrong-width argument records. It now follows PHIs transactionally with
  per-value memoization and cycle detection, accepts only identical signatures,
  and also recognizes direct function-address leaves. Conflicting or partly
  unprototyped MIR inputs remain unknown rather than borrowing one arm's
  signature. Second, verifier recovery exposed that AST lowering had already
  lost the common prototype of a conditional callee: a valid
  `long(long)` conditional call was reproduced failing strict MIR emission
  because its `int` argument had not been widened. AST callable resolution now
  forms a conservative C-compatible composite across function designators,
  local pointers, explicit function addresses, null and cast-null arms,
  compatible old-style/prototyped declarations, and separately allocated
  returned-callable prototypes. Incompatible conditional signatures are
  rejected explicitly. Indirect `__fastcall` remains rejected because the
  indirect emitter does not implement its register ABI.
- The Wave 33 real-source campaign covers direct, local-pointer, explicit
  address, null, cast-null, returned, old-style/prototyped, and nested returned
  callable PHIs. Its 96 stack/no-stack, peep/nopeep, full-debug, and line-debug
  configurations pass, raising the exact inventory from 9,590 to 9,686.
  Normal and ASan/UBSan host suites pass 5/5; exact parent comparisons retain
  all 3,039 functions with zero selector, output-hash, or app changes in stack
  and no-stack modes; all four debug censuses emit 3,039/3,039 functions; both
  strict 506-app release modes pass 482 with 24 documented skips, zero failures,
  and zero performance regressions; 392 extended configurations, seven
  required-emission controls, 10 debugger-host tests, runtime safety audits,
  and all 129 script tests pass. The mutation suite has one passing baseline
  and 16/16 specifically killed mutants, including PHI prototype transport,
  conditional prototype recovery, and incompatible-conditional rejection.
  Focused current-tree coverage gives the signature matcher 23/23 regions,
  15/15 lines, and 16/18 branch outcomes; its remaining pair guards malformed
  negative or excessive parameter counts. This focused profile does not
  replace Wave 30. Commit `5d1bf884` contains the production fix and proofs.
- Wave 34 closes the remaining scalar-call verification gap for casts, fields,
  PHIs, and returned callable expressions. Lowering now stores an owned
  signature snapshot by call ID before lowering arguments; verification gives
  that snapshot precedence over best-effort value-graph reconstruction.
  Explicit unprototyped snapshots also prevent an unrelated same-named global
  prototype from being borrowed. Per-function storage grows geometrically,
  is cleared at every `mir_begin_function`, and is retained only as compiler
  metadata; selection and emitted bytes are unchanged. Aggregate-call
  signature snapshots remain separate future work.
- Wave 34 reuses the 96 callable-PHI and 12 `qualexpr` configurations, all 108
  of which pass; the complete inventory therefore remains 9,686. Normal and
  ASan/UBSan host suites pass 5/5. Exact parent comparisons retain all
  3,039 functions with zero selector, output-hash, or app changes in stack and
  no-stack modes. Both strict 506-app release modes pass 482 with 24 documented
  skips, zero failures, and zero performance regressions; all four debug
  censuses emit 3,039/3,039 functions; 392 extended configurations, seven
  required-emission controls, 10 debugger-host tests, runtime safety audits,
  and all 129 script tests pass. The mutation suite has one passing baseline
  plus 18/18 killed mutants; the two new mutants independently disable stored
  signatures and the scalar lowering call site. Focused coverage reaches
  `mir_record_call_signature` at 21/23 regions, 34/36 lines, and 14/16 branch
  outcomes, and `mir_resolve_call_prototype` at 26/27 regions, 39/40 lines,
  and 20/24 branch outcomes. Commit `b4f870d8` contains the snapshot and proofs.
- Wave 35 adds a direct allocation invariant for a narrow value live across an
  ordinary call. After liveness and allocation, the value must be in
  callee-saved IY or a spill slot, never caller-saved HL, DE, or BC. A
  clean-build compiler mutant removes the `cross_call` classification and is
  killed by that exact assertion. The unchanged `tmirslot.cross_call` runtime
  oracle passes peep and nopeep in stack and no-stack modes. Normal and
  ASan/UBSan host suites pass 5/5, and the mutation suite now has one passing
  baseline plus 19/19 killed mutants with no survivors or invalid results.
  This test-only increment changes neither the 9,686-leaf inventory nor
  production output and does not claim a raw coverage increase. Commit
  `9cdf818e` contains the invariant and mutant.
- Wave 36 extends the allocation proof to a derived 32-bit value live across an
  ordinary direct call. The wide-coloring probe must assign that value a spill:
  neither HL:DE nor BC:IY is wholly callee-saved. The valid host control passes,
  while a clean-build mutation that admits cross-call wide pair colors is
  killed by the exact spill assertion. Normal and ASan/UBSan host suites pass
  5/5; the complete mutation campaign has one passing baseline plus 20/20
  killed mutants; strict `tmirslot` stack/no-stack runs pass peep and nopeep
  with zero performance regressions; and all 26 existing `phi-alias-wave32`
  target configurations pass. This test-only increment leaves the 9,686-leaf
  inventory and production output unchanged and makes no additive raw coverage
  claim. Commit `465ce55f` contains the invariant and mutant.
- Wave 37 proves the narrow guarded exception for a late PHI physically crossing
  a direct void call. Allocation deliberately retains the PHI in caller-saved
  DE only because the general stack-call emitter surrounds the call with an
  ordered `push de` and `pop de`. A clean-build mutation that suppresses DE
  preservation is killed by the emitted-order assertion. Normal and
  ASan/UBSan host suites pass 5/5; the complete mutation campaign has one
  passing baseline plus 21/21 killed mutants; strict `tmirlife` stack/no-stack
  runs pass peep and nopeep with zero performance regressions; and all 26
  existing `phi-alias-wave32` target configurations pass. Specialized call
  paths remain excluded by the production guard. This test-only increment
  leaves the 9,686-leaf inventory and production output unchanged and makes no
  additive raw coverage claim. Commit `ee97b296` contains the invariant and
  mutant.
- Wave 38 proves the corresponding wide guarded exception through the
  production regional homed path. A late `long` PHI physically crossing a
  direct void call receives BC:IY, and emission surrounds that call with
  ordered `push iy`, `push bc`, `pop bc`, and `pop iy`. A clean-build mutation
  that suppresses BC:IY preservation is killed only by the paired-preservation
  assertion. Normal and ASan/UBSan host suites pass 5/5; the complete mutation
  campaign has one passing baseline plus 22/22 killed mutants; strict
  `tmirlife` stack/no-stack runs pass peep and nopeep with zero performance
  regressions; and all 26 existing `phi-alias-wave32` target configurations
  pass. This test-only increment leaves the 9,686-leaf inventory and
  production output unchanged and makes no additive raw coverage claim. Commit
  `e65393ab` contains the invariant and mutant.
- Wave 39 proves the def-use cache boundary after isolated global-field value
  numbering. The existing valid graph now primes the definition cache, removes
  a redundant field load, and requires the eliminated value's definition to
  disappear before any later verifier reset. A clean-build mutation removes
  only the pass's final invalidation and is killed by the exact
  `mir_definition` cached-versus-uncached mismatch; the harness classifies that
  controlled fatal separately from crashes and ordinary assertion failures.
  Normal and ASan/UBSan host suites pass 5/5; all mutation-harness unit tests
  pass; the complete campaign has one passing baseline plus 23/23 killed
  mutants; and 72 generated fuzz target configurations pass with cache
  verification enabled. This test-only increment leaves the 9,686-leaf
  inventory and production output unchanged and makes no additive raw coverage
  claim. Commit `df67f458` contains the invariant and mutant.
- Wave 40 proves the regional paired-byte adjacency boundary independently of
  ordinary target selection. The compile probe requires the adjacent control to
  emit `;@dcc.mir paired-byte-call` and the field-gap form to omit it. A
  clean-build mutant disables only the nonadjacent-offset rejection and is
  killed by that exact assertion; crashes, build failures, and unrelated
  diagnostics are invalid outcomes. The complete campaign has one passing
  baseline plus 24/24 killed mutants. Together with the 12 target executions,
  this proves both the guard's necessity and correct forced-regional fallback.
  Production code is unchanged, and this increment makes no additive raw
  coverage claim.
- Wave 41 adds a harmless CFG branch near match to the paired-byte fixture.
  Normal selection must name and execute a generic emitter; forcing `regional`
  must fail with both the intended `read_pair` validation message and the
  unsafe-stream fatal. All five controls pass in stack/no-stack and peep/nopeep
  modes, for 20 executions. The frozen built-in inventory is 5,076 leaves.
  Production code is unchanged, and the latest exact coverage snapshot
  predates this test-only increment.
- Wave 42 adds the symmetric independent-dominance check for a missing first
  PHI predecessor. It calls `mir_verify_dominance` directly, preventing the
  structural verifier from consuming the malformed graph first. Focused LLVM
  18 coverage changes `slot_predecessors[0] >= 0` from 50 true / 0 false to
  50 true / 1 false. Normal and ASan/UBSan host tests pass 5/5, all 134 script
  tests pass, and the compiler campaign retains one baseline plus 24/24 killed
  mutants. Production output is unchanged; exact aggregate totals remain
  pending recollection.
- Wave 43 closes the next `mir_value_number_global_field_loads` proof gap
  without changing production code. The existing positive control already
  proved redundant isolated-field load elimination; two new host graphs now
  prove the missing barriers: a call-safe intervening call plus same-field
  store must not leave a stale cached value reusable, and a field whose
  whole-file scan sees two textual writers must be evicted across any
  intervening call. Current code passes both graphs, so this increment proved
  an untested barrier rather than fixing a miscompile. The clean-build mutant
  `global-field-vn-call-barrier` disables only the non-call-safe call eviction
  and is killed by the exact `FAIL isolated global field unsafe-call barrier`
  assertion. Normal and ASan/UBSan host tests pass 5/5, the full compiler
  mutation campaign has one passing baseline plus 25/25 killed mutants, and
  all 136 Python script tests pass. Commit `31c13e7c` contains the invariant
  and mutant.
- Wave 44 closes the next homed generic-fallback proof gap without changing
  production code. Existing malformed-MIR transaction tests already exercised
  nearby `MIR_PARAM`, `MIR_LOAD_INDIRECT`, and `MIR_COPY_AGGREGATE` shapes,
  but they did not prove the exact `mir_homed_reject` surface. New host
  controls now capture `DCC_MIR_HOMED_REPORT` and require the precise
  `parameter-object`, `parameter-type`, `indirect-load-type`, and
  `aggregate-copy-size` rejections, each with empty-output rollback and a
  repaired same-stream retry that matches a clean control byte-for-byte. The
  clean-build mutant `homed-aggregate-copy-size` disables only the aggregate
  size guard and is killed by the exact
  `FAIL homed aggregate copy exact rejection` assertion. Normal and
  ASan/UBSan host tests pass 5/5, the full compiler mutation campaign has one
  passing baseline plus 26/26 killed mutants, and all 136 Python script tests
  pass.
- Wave 45 closes the next `mir_match_compound_check_runner` proof gaps
  without changing production code. The earlier compound campaigns already
  proved the baseline exact schedule plus selected ABI and width mutations,
  but they did not force generic fallback for a volatile failure flag, an
  extra helper call, a harmless extra CFG block, a VLA-bearing near match, or
  a fixed-prototype success printer, and they left several
  local-address/index/member/indirect legality branches without direct
  mutation evidence. New `compound-wave45` MIR-clobber cases add those five
  source near matches plus four direct selector-mutant cases, for 40 passing
  target configurations. The dedicated `compound-wave45-audit.py` script now
  runs 24 stack/no-stack and peep/nopeep runtime controls and rejects 13/13
  targeted MIR mutations covering store source range, check-call identity,
  local-address and pointer-load identity, indirect-load address/type,
  index-address source/type, member offset/width, indirect-store value kind,
  and the final failure-load type. Current code already rejected every new
  near match and mutation, so this increment closes proof gaps rather than
  fixing a false acceptance. All 136 Python script tests pass.
- Wave 46 closes the next `mir_match_symbol_insert_schedule` proof gaps
  without changing production code. The earlier symbol-insert campaigns
  already covered the baseline exact schedule, a broad field-mutation census,
  and selected limit/copy/field-offset near matches, but they did not keep a
  focused campaign over exact fallback reasons for return-shape drift, direct
  error/copy helper signature changes, a non-canonical memset target, or
  count/name/field-store argument rewires. New `symbol-insert-wave46`
  MIR-clobber cases add five source near matches plus eight direct
  selector-mutant cases, for 56 passing target configurations. The dedicated
  `symbol-insert-wave46-audit.py` script now runs 24 stack/no-stack and
  peep/nopeep runtime controls and rejects 8/8 targeted MIR mutations
  covering the error call's string source, memset destination argument,
  strncpy destination argument, the indexed-record count source, and the
  kind/scope/size/element-size store value sources. Current code already
  rejected every new near match and mutation, so this increment closes proof
  gaps rather than fixing a false acceptance. All 136 Python script tests
  pass.
- Wave 47 closes the next spilled generic-fallback proof gaps without
  changing production code. Existing malformed-MIR transaction tests already
  exercised nearby `MIR_LOAD_INDIRECT`, `MIR_CALL`, `MIR_CALL_AGGREGATE`, and
  `MIR_VLA_SIZE` shapes, but they did not prove the exact
  `mir_scalar_cfg_preflight_reject` reasons reported by spilled generic
  preflight. New host controls now capture `DCC_MIR_SELECT_REPORT` and require
  the precise `indirect-width`, `call-abi`, `aggregate-call-abi`, and
  `frame-offset` diagnostics, each with preserved output prefixes,
  empty-output rollback, and repaired same-stream retries that match clean
  controls byte-for-byte. The clean-build mutant `spilled-call-abi` disables
  only the empty-name branch of the direct-call ABI guard and is killed by the
  exact `FAIL spilled call ABI exact rejection` assertion. Normal and
  ASan/UBSan host tests pass 5/5, the full compiler mutation campaign has one
  passing baseline plus 27/27 killed mutants, and all 136 Python script tests
  pass.
- Wave 48 closes the next `ast_assign_supported_uncached` proof gaps without
  changing production code. Existing direct-AST host coverage already
  exercised identifier, indexed, multidimensional, and pointer-element
  assignment classes, but it did not directly assert member-pointer
  compounds, numeric bitfield conversions, `_Bool` member classification,
  dead-vs-live pointer identifier compounds, or dereferenced long/float
  compound boundaries. New host assertions now prove direct `.` and `->`
  pointer-member `+=`/`-=` support, rejection of a pointer rhs for those
  compounds, float-to-bitfield `=` conversion, bitfield `<<=`, float-to-`_Bool`
  member `=` acceptance with compound rejection, dead-result pointer-identifier
  `+=` acceptance with live-result rejection, dereferenced long `>>=`,
  dereferenced float `+=`, and dereferenced float `%=` rejection.
  `tests/mir-clobber/assigncv.c` now adds a cheap end-to-end target proof for
  local pointer compounds, `box_pointer` member-pointer compounds, and
  bitfield compound stores; manual `dccmake` peep/nopeep runs both report
  `assignment coverage failures=0`. Normal and ASan/UBSan MIR host CTest pass
  5/5, all 136 Python script tests pass, and no clean compiler mutant was
  added because these classifier-only cases do not expose a narrow existing
  mutation hook with a distinct downstream oracle.
- Wave 49 closes the next `mir_match_ctype_realloc_schedule` proof gaps
  without changing production code. The earlier ctype/realloc coverage already
  proved the baseline exact schedule, the broad Wave 21 field-mutation census,
  the wave7 fastcall near matches, and selected ABI, width, and string
  mutations, but it did not keep a focused campaign over fixed-prototype
  failure/success printers, grow/shrink helper identity consistency,
  variadic-compare drift, late check-helper consistency, or several still
  unpinned pointer-slot, argument-source, and stride predicates. New
  `ctype-realloc-wave48` MIR-clobber cases add seven exact/generic runtime
  controls plus five runtime-safe selector-mutant controls, for 48 passing
  target configurations. The dedicated `ctype-realloc-wave48-audit.py` script
  now runs 28 stack/no-stack and peep/nopeep runtime controls and rejects
  18/18 targeted MIR mutations covering pointer-store/load identity,
  allocation/grow/shrink null-test operators, allocation/grow/final failure
  argument ordering, copy/preserve dataflow, resize/check helper identity,
  byte-store and byte-check stride, byte-check normalization, free-call
  argument indexing, and the final success constant. Current code already
  rejected every new near match and mutation, so this increment closes proof
  gaps rather than fixing a false acceptance. All 136 Python script tests
  pass.
- Wave 50 closes the next `mir_match_vla_smooth` proof gaps without changing
  production code. The earlier VLA smoothing coverage already proved the exact
  baseline, stack and debug modes, volatile and near-match source rejection,
  alias/stride/restoration runtime behavior, and 54 direct metadata/ABI/object
  mutations through the dedicated host harness plus `tests/mir-clobber/vla18.c`,
  but it did not directly pin the matcher's remaining top-level relation
  guards. `tests/host/mir_vla_smooth_isolation.c` now adds a
  touching-but-non-overlapping local-layout acceptance control plus 112 direct
  branch mutations covering the secondary parameter ABI checks,
  parameter/local object-use mismatches, same-slot alias drift,
  constant/value-link breakage, outer/inner loop relations, valid-index
  PHI/branch plumbing, accumulation and increment links, average-store
  wiring, alias-compare edges, and the return graph. Current code already
  rejected every new mutation and accepted the boundary-layout control, so
  this increment closes proof gaps rather than fixing a false acceptance.
  Normal and ASan/UBSan MIR host CTest each pass
  `mir-vla-smooth-isolation`, and all 136 Python script tests pass.
- Wave 51 closes the next `mir_resolve_deferred_metadata` proof gaps without
  changing production code. Existing host deferred-metadata coverage already
  proved function-pointer insertion, direct-call conversion repair,
  coordinate updates, basic alias bounds, and malformed call ordering, but it
  did not directly pin alias windows with explicit scope labels, for-init
  loop-exit truncation, orphaned `MIR_OBJECT_MERGE` demotion, or the
  `#b`-gated scoped unary/PHI type-repair loop. New direct MIR assertions now
  prove scope-label alias renaming plus `base_name` repair, label-before-window
  non-repair, forward exit-branch truncation while ignoring a backward target,
  invalid array-object merge demotion to `MIR_ADDRESS` while leaving a valid
  merge intact, and unary/PHI type repair after a block alias retargets a
  named load. The clean-build mutant `deferred-merge-demotion` disables only
  the invalid-merge fallback and is killed by the exact
  `FAIL deferred metadata merge demotion` assertion. Current code already
  satisfied every new invariant, so this increment closes proof gaps rather
  than fixing a false acceptance. Normal and ASan/UBSan MIR host CTest pass
  5/5, the full compiler mutation campaign has one passing baseline plus
  28/28 killed mutants, and all 136 Python script tests pass.
- Wave 52 fixes a real, independently reproduced defect found by the fresh
  post-wave-51 coverage checkpoint collection, not a proof gap. Compiling
  `tests/mir-clobber/cmpw4.c`'s new `CMPW45_EXTRA_HELPER_CALL` variant
  (a genuinely empty `static void` helper, `insns=1 values=0`) immediately
  after a normal-sized function fataled with
  `DCC_MIR_CACHE_VERIFY=1`: `mir_definition` returned a stale, out-of-range
  cached answer left over from the prior function. `mir_definition`,
  `mir_value_use_count`, and `mir_call_uses_value` all bounds-check their
  cached arrays against `mir_use_cache_count_capacity` /
  `mir_use_cache_arg_head_capacity`, high-water marks that never shrink
  between functions, but `mir_ensure_use_cache`'s reset loop only cleared
  indices up to the *current* function's own smaller
  `next_value`/`next_call_id`. A function with fewer values or calls than an
  earlier one left high indices holding the earlier function's cached
  def-index/arg-head answers, readable as if valid for the new function.
  This was reproducible directly (`DCC_MIR_CACHE_VERIFY=1 ./dcc ... cmpw15.c`
  fataled before the fix, exit 0 after) and is a real latent
  miscompile risk in ordinary (non-cache-verified) builds: any pass that
  queries `mir_definition`/`mir_value_use_count`/`mir_call_uses_value` for an
  index unused by a small function could silently receive a wrong,
  unrelated instruction from an earlier function. The fix clears the full
  allocated capacity on every cache rebuild instead of only the current
  function's smaller count. A new permanent host control
  (`verify_use_cache_capacity_reset_across_functions`) reproduces the exact
  shape directly (a normal function defining value 0 at instruction 1,
  immediately followed by a zero-value function) and was confirmed to fail
  with the fix reverted before being restored; `MIR verifier failures=0`
  with the fix in place. A full stack/no-stack selector census against the
  pre-fix parent shows zero changed selections, zero changed output, and
  zero apps requiring runtime validation across all 3,039 functions,
  confirming this was a latent, previously-undetected bug rather than a
  change to any existing production selection or output. Normal and
  ASan/UBSan MIR host tests pass, the full compiler mutation campaign has
  one passing baseline plus 28/28 killed mutants, both strict stack/no-stack
  full+extended release gates pass with zero failures and zero performance
  regressions, and all 136 Python script tests pass.
- Wave 53 closes the next `mir_match_byte_math_flags` proof gaps without
  changing production code. Existing byte-math coverage already proved the
  exact baseline, the broad Wave 19 field-mutation census, and the source
  mask/compare/complement/add/overflow/logic near matches, but it did not
  keep a focused campaign over helper prototypes, variadic call-metadata
  drift, or top-level non-void/VLA shape rejection, and it left several named
  fallback reasons unpinned in the standalone audit. `tests/mir-clobber/
  bytemath.c` now adds ANSI helper, compare-variadic, decimal-variadic,
  non-void-return, and VLA source variants. The dedicated
  `byte-math-wave53-audit.py` script runs 24 stack/no-stack and peep/nopeep
  runtime controls, retaining exact selection for the baseline and ANSI-helper
  variants while forcing spilled generic fallback for the four near matches,
  and rejects 7/7 targeted MIR mutations covering instruction-metadata type
  drift, wide-store width, state-pointer typing, compare/decimal call
  indirection, and both early return-value paths. Current code already
  accepted or rejected every new control as intended, so this increment closes
  proof gaps rather than fixing a false acceptance. All 136 Python script
  tests pass.
- Wave 60 closes the next `mir_match_catalan_driver_schedule` proof gaps
  without changing production code. Existing Catalan coverage already proved
  the exact baseline, the alternate `_pflio` full-I/O exact path, renamed
  helpers, unsigned/volatile source near matches, and the broad Wave 23
  compile-only field census, but it did not keep a focused runtime-backed
  proof over helper-identity drift across the `zero`/`is_zero`/`add_term`/
  `div_small` families, fixed-print and wrapped-`putchar` near matches, or
  the remaining metadata, initializer, report, and print-loop legality
  checks. `tests/mir-clobber/catw23.c` now adds six source-level wrapper
  controls (second-array `zero`, `is_zero`, `add_term`, and `div_small`
  indirection plus fixed-print and wrapped-`putchar` variants), and the new
  `scripts/mir-clobber-cases/catalan-wave60.json` group adds 11 runtime-safe
  selector mutants. All 72 target configurations pass. The dedicated
  `catalan-wave60-audit.py` script runs 28 stack/no-stack and peep/nopeep
  runtime controls and rejects 11/11 targeted MIR mutations covering
  helper-identity drift, metadata and array-initializer mutations, both loop
  headers and tails, the initial report argument source, and the outer-print,
  inner-print, digit, and newline tails. Current code already rejected every
  new near match and mutation, so this increment closes proof gaps rather
  than fixing a false acceptance. All 136 Python script tests pass.
- Wave 62 closes the next `mir_match_symbol_find_schedule` proof gaps without
  changing production code. The earlier symbol-find coverage already proved
  the exact baseline, the broad Wave 21 field-mutation census, capacity and
  memory boundaries, unsigned globals/fields/indexes, volatile table
  rejection, and comparison-call global-clobber safety, but it did not keep a
  focused runtime-backed proof over unsigned return shape, variadic compare
  and error helpers, void/variadic copy helpers, or count/table address
  escapes. New `symbol-find-wave62` MIR-clobber cases add those seven source
  near matches plus ten runtime-safe selector mutants, for 72 passing target
  configurations. The dedicated `symbol-find-wave62-audit.py` script runs 32
  stack/no-stack and peep/nopeep runtime controls and rejects 19/19 targeted
  MIR mutations covering unsigned scalar and member-pointer types, PHI/loop/
  compare/copy/store/return dataflow, direct helper identity/indirection, and
  the memory-limit upper boundary. Current code already rejected every new
  near match and mutation, so this increment closes proof gaps rather than
  fixing a false acceptance. All 136 Python script tests pass.
- Wave 63 closes the next `mir_match_ptr_condition_main` proof gaps without
  changing its accepted program set or generated schedule. The complete
  semantic signature is now checked after the matcher's explicit constant,
  call, ABI, global, alias, and aggregate-layout proofs, so mutations of those
  fields reach their specific rejection paths instead of being hidden by the
  earlier catch-all signature rejection. `tests/tptrcnd.c` adds runtime-safe
  alternate init/fail/check/picker helpers, a volatile failure counter, and
  renamed-global and loop-picker alias controls. The dedicated
  `pointer-condition-wave63-audit.py` campaign runs 52 stack/no-stack and
  peep/nopeep controls, retaining exact selection for the baseline,
  static-global, and fastcall-picker variants while proving named spilled
  fallback for ten near matches. It also rejects 19/19 targeted MIR mutations
  covering byte promotion, operation/type/layout checks, constants, call and
  argument identities, globals, local/global/function aliasing, initialization,
  and return layout, with zero meaningful survivors. No false acceptance was
  found; the baseline assembly and selected hash remain unchanged. The
  standalone audit, full Python script suite, and focused strict stack/no-stack
  `tptrcnd` release gates pass. The broader coverage objective remains
  incomplete.
- Wave 64 closes the next `mir_match_float_tangent_rational` proof gaps
  without changing production code. The historical tangent schedule had no
  dedicated fixture or focused audit. New `tests/mir-clobber/tanrat.c`
  isolates the 114-instruction exact shape and checks seven results against
  independently computed mathematical tangent values. The dedicated
  `float-tangent-wave64-audit.py` campaign runs 12 stack/no-stack and
  peep/nopeep runtime controls, retaining exact selection for the baseline
  and proving named spilled fallback for extra-arithmetic opcode/shape and
  variadic-remainder ABI near matches. It also rejects 13/13 targeted MIR
  mutations covering parameter and call types, local width and identity, call
  arguments, repeated
  constants, negation, period/quadrant/rational/result dataflow and operators,
  zero-result flow, and the final PHI. Current code rejected every mutation
  and near match as intended, so this increment closes proof gaps rather than
  fixing a false acceptance. The standalone audit and all 136 Python script
  tests pass. The broader coverage objective remains incomplete.
- Wave 65 closes the next `mir_match_exec_recursion_schedule` proof gaps
  without changing production code. The existing Wave 14 campaign already
  proved all hardcoded binary operators and word-dataflow loads with its
  127-mutation audit, plus selected ABI, width, volatility, and source
  near matches. The new `exec-recursion-wave64` clobber group adds exact
  renamed-helper/global controls; pointer-ABI, fixed-reporter, signedness,
  volatility, CFG, and VLA near matches; and 35 runtime-safe selector
  mutations covering parameter types, entry control, vector construction,
  exec/report/recursive call arguments, constants, local/global identities,
  failure side effects, branch values, and final returns. All 94 target
  configurations pass. The dedicated `exec-recursion-wave65-audit.py`
  campaign runs 48 stack/no-stack and peep/nopeep runtime controls and
  rejects 35/35 targeted mutations with named spilled fallback and zero
  meaningful survivors. Current code already rejected every new near match
  and mutation, so this increment closes proof gaps rather than fixing a
  false acceptance. All 136 Python script tests pass. The broader coverage
  objective remains incomplete.
- Wave 70 closes the historical `mir_match_whitespace_scan_schedule` proof
  gap without changing production code. Commit `0401e793` introduced the
  exact schedule before focused per-matcher audits, and no dedicated fixture
  or campaign remained in the tree. New `tests/mir-clobber/wsscan.c` isolates
  the 60-instruction, eight-block schedule and checks bounded, empty,
  multiline, and helper-mutated state against independent cursor, line, and
  call-count oracles. The dedicated `whitespace-scan-wave70-audit.py` campaign
  runs 16 stack/no-stack and peep/nopeep runtime controls, retaining exact
  selection for baseline and renamed-helper forms while proving hybrid generic
  fallback for variadic-helper and extra-CFG near matches. A forced
  `DCC_MIR_SELECT_CANDIDATE=hybrid` cost control confirms that the clean
  scheduled stream is the exact incumbent before the requested diagnostic
  alternative is selected. The campaign rejects 25/25 targeted MIR mutations
  covering signed bounds, source/index/byte flow, helper identity and ABI,
  short-circuit PHIs, post-call reloads, newline comparison, line/cursor
  updates, and state overlap/range checks, with zero meaningful survivors.
  Current code already rejected every new near match and mutation, so no
  genuine false acceptance was found. The clean selected hash remains
  `0259e664` and assembly SHA-256 is
  `31c83b5f4d79640c9908a480717fa7a952afd7d570e3069daebc3860cb92850b`.
  The standalone audit and all 136 Python script tests pass. The broader
  coverage objective remains incomplete.
- Wave 71 closes the historical `mir_match_random_wide_fill` proof gap and
  fixes genuine selector false acceptances. New
  `tests/mir-clobber/rndwide.c` isolates the 37-instruction, four-block
  schedule and checks eight deterministic wide results against a fixed oracle.
  The dedicated `random-wide-fill-wave71-audit.py` campaign runs 24
  stack/no-stack and peep/nopeep runtime controls, retaining exact selection
  for baseline and renamed-helper forms while proving spilled generic fallback
  for helper-width, count-signedness, volatile-destination, and
  volatile-temporary near matches. A forced
  `DCC_MIR_SELECT_CANDIDATE=spilled-phi-slot` cost control confirms that the
  clean scheduled stream is the exact incumbent before the requested
  diagnostic alternative is selected. The matcher now proves exact scalar
  types, branch and increment dataflow, local-store identity, memory width and
  volatility, and the direct helper ABI. The campaign rejects 38/38 targeted
  MIR mutations with zero meaningful survivors; before the fix, mutations of
  the branch condition, increment step, local identities, and multiple type
  fields retained the unchanged exact schedule. The clean selected hash
  remains `2dc38a8d` and assembly SHA-256 is
  `464c9dc3af8d8081d2548e3049bae1cf16623bf6309f31a387c95db0f7662a7c`.
  The standalone audit, all Python script tests, and both strict 506-app
  release modes pass. The broader coverage objective remains incomplete.
- Wave 80 closes the historical `mir_match_fixed_embedding_build` proof gap
  without changing production code. Commit `9299371d` introduced the exact
  schedule before focused per-matcher audits, and no dedicated fixture or
  campaign remained in the tree. New `tests/mir-clobber/fxembd.c` isolates
  the 77-instruction, seven-block schedule and validates all 128 embedding
  outputs, including 16 lower and 16 upper saturations, against an
  independently indexed and clamped oracle. The dedicated
  `fixed-embedding-wave80-audit.py` campaign runs 12 stack/no-stack and
  peep/nopeep runtime controls, retaining exact selection for the baseline
  while proving named spilled fallback for volatile-token and variadic-clamp
  near matches. `DCC_MIR_COST_REPORT` identifies the exact incumbent and
  `DCC_MIR_SELECT_CANDIDATE=spilled-phi-slot` independently confirms every
  rejected mutation's generic fallback. The campaign rejects 24/24 targeted
  MIR mutations covering global and local identities, initializers, PHI and
  loop bounds, token/weight indexing, widths, pointer increments, signed-wide
  promotion/addition, call ABI/dataflow, result storage, and loop updates,
  with zero meaningful survivors. Current code already rejected every near
  match and mutation, so no genuine false acceptance was found. The clean
  selected hash remains `e979e278` and assembly SHA-256 is
  `399f0d1374c4e85d8da4744ccaf930010a55a07c7169e4675822bdb36ec7f2e4`.
  The standalone audit and all 136 Python script tests pass. The broader
  coverage objective remains incomplete.
- Wave 82 closes the historical
  `mir_match_packed_byte_report_schedule` proof gap without changing
  production code. No dedicated fixture or audit previously covered this
  exact schedule. New `tests/mir-clobber/pkbrpt.c` isolates its 34-instruction,
  one-block shape and checks an asymmetric four-byte packing result against an
  independently computed runtime oracle. The dedicated
  `packed-byte-report-wave82-audit.py` campaign runs 20 stack/no-stack and
  peep/nopeep runtime controls, retaining exact selection for baseline and
  renamed-helper forms while proving spilled generic fallback for volatile,
  word-width, and VLA buffer near matches. A forced
  `DCC_MIR_SELECT_CANDIDATE=hybrid` cost control confirms that the clean
  scheduled stream is the exact incumbent before diagnostic selection. The
  campaign rejects 26/26 targeted MIR mutations covering buffer type and
  identity, lane indices, address dataflow, stride and memory width, byte
  constants, store operands, pack-call identity/arguments/directness/result
  width, print string/arguments/directness/result width, and the zero return,
  with zero meaningful survivors. Current code already rejected every new
  near match and mutation, so no genuine false acceptance was found. The clean
  selected hash remains `c1803b97` and assembly SHA-256 is
  `6c82b5625ae46dd0fef759670274f489aa0656b8b9a0ffc77e17b1f921c1e47b`.
  The standalone audit and all 136 Python script tests pass. The broader
  coverage objective remains incomplete.
- Wave 90 closes the remaining focused
  `mir_match_call_safe_member_sum_schedule` proof gaps without changing
  production code. The existing Wave 17 clobber manifest already covered the
  exact baseline, local and aliasing callees, volatile and CFG near matches,
  and 13 broad field mutations. The dedicated
  `call-safe-member-sum-wave90-audit.py` campaign extends that evidence with
  32 stack/no-stack and peep/nopeep runtime controls, retaining exact
  selection for baseline, local-callee, and aliasing-callee forms while
  proving named spilled fallback for variadic, different-callee, padded-record,
  and volatile-loop-state near matches. The campaign rejects 47/47 new MIR
  mutations covering word types and widths, parameter and local identities,
  initializers, PHIs, loop condition dataflow, member layout and loads, direct
  call structure, every staged call sum, raw-member accumulation, increment,
  and final store flow. Each rejection is independently reproduced through
  forced `DCC_MIR_SELECT_CANDIDATE=spilled-phi-slot` output, with zero
  meaningful survivors. Current code already rejected every new near match and
  mutation, so no genuine false acceptance was found. The clean selected hash
  remains `99a108e6` and assembly SHA-256 is
  `865bd210a681c96c6ee1d767a513ab0ca24e169818f8d86e378a3de9c8be0534`.
  The standalone audit and all 136 Python script tests pass. The broader
  coverage objective remains incomplete.
- Wave 100 closes the historical `mir_match_gnarly_runner` proof gap and fixes
  a genuine exact-schedule false-acceptance class. The 564-instruction,
  22-block matcher already proved the complete opcode fingerprint, constants,
  value and PHI relationships, branch targets, conversions, 39-call ABI,
  distinct and reused strings, array identities and strides, structure-copy
  layout, object numbering, and return flow. It did not prove result types for
  its binary and PHI nodes or pointer/member types used by its hard-coded
  array and structure accesses; diagnostic type mutations therefore retained
  the unchanged exact schedule. The matcher now checks signed-word versus
  word-pointer binary results, signed-word PHIs, both Duff arrays, every main
  array address/index result, and word/byte structure-member addresses.
  New `tests/mir-clobber/gnarly.c` retains the original language-stress shape
  while its called helpers independently validate the copy, structure,
  implicit-call, function-pointer, and old-style-call path and report
  `gnarly oracle failures=0`. The dedicated
  `gnarly-runner-wave100-audit.py` campaign runs eight stack/no-stack and
  peep/nopeep runtime controls, retaining exact selection for the baseline and
  proving named spilled fallback for a volatile-count near match. It rejects
  22/22 targeted MIR mutations across constants, operations, PHIs, control
  flow, conversions, strings, direct/indirect and variadic calls, array
  identities/types/widths, structure-copy members, and object identity.
  Forced `DCC_MIR_SELECT_CANDIDATE=spilled-phi-slot` output independently
  confirms every generic fallback with zero meaningful survivors. The clean
  selected hash remains `a855a26c` and assembly SHA-256 is
  `dc95b353f36d0e1245a68d34b22f3ed7fafdff6bdba02a991fbd1fecb22cbfbb`.
  The standalone audit, all Python script tests, and both strict 506-app
  release modes pass. The broader coverage objective remains incomplete.
- Wave 150 closes the historical `mir_match_float_atan2_schedule` proof gap
  and fixes a genuine exact-schedule false-acceptance class. The matcher
  previously checked the 66-opcode fingerprint, selected constants, partial
  value flow, parameter layout, and unary helper prototypes, but omitted
  comparison/arithmetic result types, branch-value and several return-value
  relationships, ratio-local identity and width, direct-call form, and the
  complete CFG label relationships. Twenty-four meaningful diagnostic field
  mutations across those omissions retained the unchanged exact schedule.
  The matcher now proves the ten-block label graph, exact float/integer types,
  nonvolatile parameter/load/local locations, the ratio store, all branch and
  return dataflow, and the complete direct non-variadic helper ABI.
  New `tests/mir-clobber/fatan2.c` isolates the schedule and validates nine
  independently tabulated arctangent results across the origin, axes,
  quadrants, and asymmetric coordinates. The dedicated
  `float-atan2-wave150-audit.py` campaign runs 20 stack/no-stack and
  peep/nopeep runtime controls, retaining exact selection for the baseline
  while proving spilled fallback for variadic-helper, volatile-parameter,
  volatile-ratio, and different-helper near matches. It rejects 34/34
  targeted MIR mutations and independently forces
  `DCC_MIR_SELECT_CANDIDATE=spilled-phi-slot` for every fallback, with zero
  meaningful survivors. The clean selected hash remains `77d2fef8` and
  assembly SHA-256 is
  `98d3715cb1c1fcca5d9801b075cf97fda73eb079bbb35a0e8bbf37d24ab22213`.
  The standalone audit, all Python script tests, and both strict 506-app
  release modes pass. The broader coverage objective remains incomplete.
- Wave 140 closes the historical
  `mir_match_matrix_product_schedule_kind` proof gap for both the 101-
  instruction transposed and 90-instruction outer schedules and fixes a
  genuine exact-schedule false-acceptance class. The matcher already proved
  both opcode fingerprints, seven-block loop structure, parameter layout,
  counter and pointer dataflow, per-product conversion and saturating-add call
  identities, and final stores. Fresh diagnostic mutations showed that many
  named-memory widths, pointer/count/word/long result types, binary operand
  types, and indexed/indirect access types were not part of that proof: 73 of
  85 representative mutations retained the unchanged hard-coded schedule.
  The matcher now validates those type and width invariants for both kinds and
  tightens the transposed `memset` call and argument metadata.
  New `tests/mir-clobber/matkind.c` contains both exact source functions and
  independently calculated matrix-result oracles, including saturation and
  negative fixed-point products. The dedicated
  `matrix-product-kind-wave140-audit.py` campaign runs 12 stack/no-stack and
  peep/nopeep runtime controls, retains both exact schedules in clean builds,
  isolates source near matches for each kind, and rejects 95/95 targeted MIR
  mutations (49 transposed and 46 outer). Forced
  `DCC_MIR_SELECT_CANDIDATE=spilled-store-address` output independently
  confirms every generic fallback with zero meaningful survivors. The clean
  selected hashes remain `88cdd4b5` and `eca25a44`; combined fixture assembly
  SHA-256 is
  `b3d565637021d445de314147095b9e7aba6fcd7a5de036f89e130d952a263452`.
  The standalone audit, full Python script-test suite, and both strict 506-app
  release modes pass. The broader coverage objective remains incomplete.
- Wave 170 closes the historical `mir_match_aggregate_field_sum` proof gap
  and fixes genuine exact-schedule false acceptances. The matcher already
  constrained the one-block opcode counts, three-leaf addition tree,
  nonvolatile scalar loads, common aggregate parameter location, field
  offsets, widening conversions, and wide return. Fresh diagnostic mutations
  showed that aggregate parameter types, aggregate-address and member-pointer
  types, member widths, and load widths were not consistently proved. Those
  gaps let 24 of 34 representative mutations retain the exact schedule,
  including cases where the emitter changed the number of bytes read from a
  field.
  The matcher now validates the aggregate parameter and its three addresses,
  requires scalar member-pointer and load types to agree, bounds every field
  inside the aggregate, matches member/load widths, and proves the wide
  addition result and operand types. New `tests/mir-clobber/aggfsum.c`
  isolates the 16-instruction schedule with signed-byte, signed-long, and
  unsigned-word fields and checks two asymmetric results against fixed
  independent values. The dedicated
  `aggregate-field-sum-wave170-audit.py` campaign runs 24 stack/no-stack and
  peep/nopeep runtime controls, retains exact selection for baseline, renamed,
  and padded-layout forms, and proves generic fallback for volatile, pointer,
  and four-field near matches. It rejects 34/34 targeted MIR mutations across
  parameter/address/member/load types, widths and bounds, conversions,
  addition nodes, and return flow. Forced
  `DCC_MIR_SELECT_CANDIDATE=spilled-phi-slot` output independently confirms
  every generic fallback with zero meaningful survivors. The clean selected
  hash remains `bcb40981` and assembly SHA-256 is
  `3e00c6e0a6c77e7df08684782db10084f71c228e2b1ec29cff820914bd86d7e3`.
  The standalone audit, full Python script-test suite, and both strict 506-app
  release modes pass. The broader coverage objective remains incomplete.
- Wave 200 closes the historical `mir_match_bcd_byte_math_schedule` proof gap
  and fixes a genuine exact-schedule false-acceptance class. The prior
  240-instruction matcher proved the opcode sequence, 33-block CFG, parameter
  widths and offsets, three nonvolatile state members, and selected decimal
  constants, but did not prove most result types, operand definitions,
  arithmetic operators, PHI inputs and predecessors, local identities, or
  memory widths. Before the fix, 26/29 representative mutations in those
  fields retained the unchanged hard-coded BCD emitter. The matcher now proves
  every instruction type, 152 operand-to-definition relationships, all 66
  unary/binary operators, all 32 constants, 36 memory accesses, 33 distinct
  labels, six PHI predecessor pairs, and the nine distinct parameter/local
  locations in addition to its existing state binding.
  `bcd-byte-math-wave200-audit.py` extends the existing `bmw9.c` independent
  2,048-case arithmetic oracle, runs eight stack/no-stack and peep/nopeep
  runtime controls, and proves named
  `spilled-boolean-phi-branch` fallback for a volatile-result near match and
  29/29 targeted MIR mutations. Forced
  `DCC_MIR_SELECT_CANDIDATE=spilled-boolean-phi-branch` output independently
  confirms every fallback with zero meaningful survivors. The clean selected
  hash remains `14ace686` and assembly SHA-256 is
  `4c1cc708a5bba78f085bd74af9e9ac13293e3d22b6e00587a2666a8cc202cafb`.
  The standalone audit, all 136 Python script tests, and both strict 506-app
  release modes pass. This fixes production matching but does not replace the
  immutable aggregate coverage ledger or complete the broader objective.
- Wave 220 closes the historical `mir_match_indexed_word_sum` proof gap and
  fixes genuine exact-schedule false acceptances. The matcher already required
  a one-block, one-parameter, two-load, one-add return shape, a common scalar
  pointer parameter, positive constant indices, bounded computed offsets,
  word loads, and nonvolatile accesses. It did not prove consistency among the
  parameter, index-address, member-pointer, load, addition, and return types,
  nor did it match index/member memory widths to the emitted stride and word
  accesses. Twenty-four of 47 representative mutations of those fields
  retained the exact schedule before the fix.
  The matcher now proves parameter storage/type consistency, index operand
  types and stride metadata, scalar index-constant widths, member-pointer and
  load type/width agreement, addition operand/result types, and the return
  type. New `tests/mir-clobber/idxwsum.c` isolates the 14-instruction schedule
  and checks two asymmetric signed sums against fixed independent values. The
  dedicated `indexed-word-sum-wave220-audit.py` campaign runs 32 stack/no-stack
  and peep/nopeep runtime controls, retaining exact selection for the baseline
  and proving generic fallback for volatile, bitfield, long-result,
  extra-parameter, VLA, local-state, and CFG near matches. It rejects 47/47
  targeted MIR mutations spanning every parameter, constant, index, member,
  load, addition, and return field used by the proof. Forced
  `DCC_MIR_SELECT_CANDIDATE=spilled-phi-slot` output independently confirms
  every mutation fallback with zero meaningful survivors. The clean selected
  hash remains `e3d216d0` and assembly SHA-256 is
  `a68e9c6e4b124bc3e24adf826e8b8717693927d84185d82546251c3d60c623ce`.
  The standalone audit, full Python script-test suite, and both strict 506-app
  release modes pass. The broader coverage objective remains incomplete.
- Wave 230 closes the historical `mir_match_float_asin_schedule` proof gap and
  fixes a genuine false-acceptance class. The old matcher proved the
  95-instruction opcode fingerprint, selected constants, partial call
  identity, and the Horner tree, but 52 of the campaign's 60 type, dataflow,
  width, identity, return, and direct-call mutations still retained the
  hard-coded schedule. The matcher now proves the four-block label graph,
  exact float and comparison types, nonvolatile parameter/load/local
  identities and widths, sign normalization, both domain branches and
  returns, transform and recursive-call flow, complete unary call ABI, and
  every polynomial input, store, and result relationship.
  New `tests/mir-clobber/fasin.c` isolates the exact source shape and validates
  twelve independently tabulated arcsine results across signs, the polynomial
  and transformed domains, endpoints, and the historical out-of-domain
  behavior. The dedicated `float-asin-wave230-audit.py` campaign runs 20
  stack/no-stack and peep/nopeep runtime controls, retaining exact selection
  for the baseline while proving spilled fallback for variadic-square-root,
  volatile-parameter, volatile-sign, and different-recursion near matches. It
  rejects 60/60 targeted MIR mutations and independently forces
  `DCC_MIR_SELECT_CANDIDATE=spilled-phi-slot` for every fallback, with zero
  meaningful survivors. The clean selected hash is `32a12ad4` and assembly
  SHA-256 is
  `d2b59eea697c24e27009b21cebe118b508ccd97bb667d702002f8fa1640dce89`.
  The standalone audit, full Python script-test suite, and both strict 506-app
  release modes pass. The broader coverage objective remains incomplete.
- Wave 240 closes the historical `mir_match_allocator_bridge_schedule` proof
  gap and fixes genuine ABI false acceptances. The old matcher proved selected
  allocator/free/fill identities, constants, and the final merged-pointer
  check, but did not prove the complete type, memory, value, CFG, PHI, local,
  or call-ABI contract before emitting its hard-coded allocator schedule.
  Fastcall replacements for each of allocate, free, failure, and fill could
  therefore retain a stack-call schedule with an incompatible ABI.
  New `tests/mir-clobber/albridge.c` isolates the allocator coalescing shape and
  verifies the 3,006-byte merged allocation and byte fill. The dedicated
  `allocator-bridge-wave240-audit.py` runs 44 stack/no-stack and
  peep/nopeep runtime controls across 11 source variants, retains exact
  selection for baseline and renamed functions, proves generic fallback for
  volatile, extra-CFG, and fastcall near matches, and rejects all 178 targeted
  MIR mutations with independently forced spilled fallback. The clean selected
  hash is `9064348e` and assembly SHA-256 is
  `1677aef387a62b148a677d09db0f89d3934f701ea45319424f45b1f6d5cff7fb`.
  The standalone audit, full Python script-test suite, and both strict 506-app
  release modes pass. The broader coverage objective remains incomplete.
- Wave 260 closes the historical `mir_match_indexed_member_write` proof gap
  and fixes genuine exact-schedule false acceptances. The old matcher proved
  the single-block opcode counts, destination outline, common state root,
  positive stride, bounded pointer adjustment, and parameter stack offset,
  but not the complete parameter/local memory contract, state and element
  field declarations, pointer/index/arithmetic types, or store value identity.
  Forty-three of 68 representative mutations retained the exact schedule
  before the fix. The matcher now binds both state-member accesses to the same
  typed root and declared fields, proves qualifiers, widths, offsets, stride
  and arithmetic types, verifies the local address round trip, and matches the
  destination field and stored parameter exactly.
  New `tests/mir-clobber/idxmwrit.c` isolates the indexed structure-member
  store and checks the target, adjacent guards, neighboring elements, and
  enclosing state. The dedicated `indexed-member-write-wave260-audit.py`
  campaign runs 68 stack/no-stack and peep/nopeep runtime controls across 17
  source variants, retains exact selection for baseline, renamed, adjusted,
  and reversed-add forms, and proves generic fallback for volatile, narrow,
  wide, bitfield, extra-parameter, CFG, and non-void near matches. It rejects
  all 68 targeted MIR mutations with independently forced
  `spilled-store-address` fallback and zero meaningful survivors. The clean
  selected hash remains `c37f081a` and assembly SHA-256 is
  `32f652d93d598292d2dbef07f11cc0493d27c3f8be9d68f61fe73c2708a4a9d1`.
  The standalone audit, all 136 Python script tests, and both strict 506-app
  release modes pass. The broader coverage objective remains incomplete.
- Wave 340 closes the historical `mir_match_wraparound_bool_step` proof gap and
  fixes genuine exact-schedule false acceptances. The old matcher proved the
  94-instruction opcode fingerprint, parameter stack locations, loop/branch
  relationships, wraparound indices, XOR operands, local identities, and
  access widths, but accepted inconsistent same-width types throughout those
  operations. Thirty-six of 132 representative type, width, operator,
  dataflow, identity, and index mutations retained the exact schedule before
  the fix. The matcher now requires the exact signed word/long, boolean, and
  boolean-pointer types used by the emitted instructions, consistent binary
  operand types, nonvolatile indexed accesses, and the canonical untyped
  increment metadata.
  New `tests/mir-clobber/wrapbool.c` isolates the five-block Rule 90 step and
  checks one-, two-, and five-cell results plus zero and negative counts. The
  dedicated `wraparound-bool-step-wave340-audit.py` campaign runs 52
  stack/no-stack and peep/nopeep runtime controls across 13 source variants,
  retains exact selection for baseline and commuted-XOR forms, and proves
  generic fallback for unsigned, alternate-width, volatile, and extra-CFG
  near matches. It rejects all 132 targeted MIR mutations with independently
  forced `spilled-phi-slot` fallback and zero meaningful survivors. The clean
  selected hash is `f741fcea` and assembly SHA-256 is
  `6bf3bb37f7281a406177fccf9c8d67cd069a947a730a280ef0a621159cad78f1`.
  The standalone audit, full Python script-test suite, and both strict 506-app
  release modes pass. The broader coverage objective remains incomplete.
- Wave 520 closes the historical `mir_match_modular_product_schedule` proof gap
  and fixes genuine exact-schedule false acceptances. The old matcher proved
  the 13-instruction opcode outline, one-block CFG, selected dataflow, 32-bit
  widths, and parameter stack locations, but not unsigned parameter/arithmetic
  types, canonical casts, or unused width metadata. Twenty of 33 representative
  type, width, operator, dataflow, and parameter-identity mutations retained
  the hard-coded unsigned `__m1mu` schedule before the fix. A signed-input
  source near match also selected it and returned `2,43,31` instead of
  `3,89,31`.
  New `tests/mir-clobber/modprod.c` isolates the fused modular product and
  validates three independently tabulated results including large operands.
  The dedicated `modular-product-wave520-audit.py` campaign runs 36
  stack/no-stack and peep/nopeep runtime controls across nine source variants,
  retains exact selection for baseline and renamed functions, proves generic
  fallback for signed, narrow, volatile, alternate-return/arithmetic,
  extra-CFG, and local-state near matches, and rejects all 33 targeted MIR
  mutations with independently forced `spilled-wide-binary-lhs` fallback.
  The clean selected hash remains `8e867beb` and assembly SHA-256 is
  `329398a141cf6aa1b045f256eb26e2f15b4c0725794d733fc7f2f20adc524d4e`.
  The standalone audit, full Python script-test suite, and both strict 506-app
  release modes pass. The broader coverage objective remains incomplete.
- Wave 1000 closes the historical `mir_match_byte_record_copy_schedule` proof
  gap and fixes genuine exact-schedule false acceptances. The old matcher
  proved the 51-instruction single-block opcode fingerprint, two pointer
  parameters and stack offsets, eight byte offsets and widths, nonvolatile
  accesses, and source-to-destination SSA flow, but did not bind member-pointer
  types to the loaded byte type or either stored type to that load. Fifty-six
  of 156 representative parameter, type, offset, width, and operand mutations
  retained the hard-coded `ldir` schedule before the fix. The matcher now
  requires matching member-pointer types, a scalar byte load, the same stored
  type, and plain zero-flag memory accesses for all eight fields.
  New `tests/mir-clobber/brecopy.c` isolates the exact eight-byte record copy
  and checks two asymmetric records against fixed values. The dedicated
  `byte-record-copy-wave1000-audit.py` campaign runs 108 stack/no-stack and
  peep/nopeep runtime controls across 27 source variants, retains exact
  selection for baseline and renamed functions, and proves generic fallback
  for volatile parameters and fields, distinct record types, reversed and
  extra parameters, bitfields, local/VLA state, extra fields, and non-void
  variants. It rejects all 156 targeted MIR mutations with independently
  forced `spilled-phi-slot` fallback and zero meaningful survivors. The clean
  selected hash remains `53bdf7fb` and assembly SHA-256 is
  `e9d0990b8b4f72d1ba4962d98b8f6cd91cf8d602eba4b60b0031b8e0cd302114`.
  The standalone audit, all 136 Python script tests, and both strict 506-app
  release modes pass. The broader coverage objective remains incomplete.
- Wave 1100 closes the historical `mir_match_board_matrix_print_schedule`
  proof gap and fixes genuine exact-schedule false acceptances. The old
  matcher proved the 59-instruction, seven-block loop and call outline, the
  8-by-8 global board layout, selected row/column flow, and variadic print
  identities, but did not bind all scalar load/store widths and types, the
  column-increment reload to the column local, pointer-index types, increment
  metadata, or argument/call ABI types and flags. Thirty-three of 103
  representative type, width, index, operand-identity, call, and dataflow
  mutations retained the hard-coded board walk before the fix. The matcher
  now requires exact signed-word loop state, canonical increment metadata,
  the column reload and updates to use one nonvolatile local, bool-pointer
  index chains, clean memory metadata, and exact variadic call metadata.
  New `tests/mir-clobber/boardmx.c` isolates the schedule and checks an
  asymmetric 3-by-3 board plus a failure oracle. The dedicated
  `board-matrix-print-wave1100-audit.py` campaign runs 44 stack/no-stack and
  peep/nopeep runtime controls across 11 source variants, retains exact
  selection for baseline, renamed, and variadic-wrapper forms, proves generic
  fallback for volatile/alternate-layout boards, alternate parameter types,
  split nonvariadic print functions, extra CFG, and non-void near matches, and
  rejects all 103 targeted MIR mutations with independently forced
  `spilled-phi-slot` fallback. The clean selected hash remains `7d561397` and
  assembly SHA-256 is
  `e5027db9c92b8eade0582fa0e215731461e61ccabe8881a3ba018ef07e9b9460`.
  The standalone audit, full Python script-test suite, and both strict
  506-app release modes pass. The broader coverage objective remains
  incomplete.
- Wave 1200 closes the historical `mir_match_qsort_edge_schedule` proof gap
  and fixes genuine exact-schedule false acceptances. The old matcher checked
  only the 344-instruction/40-block envelope, one array address, seven sort
  and failure call identities, seven string-address opcodes, and 22 constants.
  It accepted 523 of 561 targeted MIR mutations: all 177 type changes, all 48
  width changes, all 114 first-operand changes, all 59 second-operand changes,
  83 of 105 immediate/stride changes, and 42 of 58 symbol/local-identity
  changes. Separate alternate-comparator and second-array source controls also
  selected the exact schedule and each failed its runtime oracle.
  The matcher now proves the exact numeric MIR semantic payload, complete CFG
  and SSA relationships, every memory flag and width, all 25 array aliases,
  all seven comparator aliases, and the sort/failure/comparator ABI. String
  IDs remain intentionally normalized because the emitter preserves the
  matched source strings.
  New `tests/mir-clobber/qsedge.c` isolates the seven qsort edge workloads and
  includes observable alternate-comparator and second-array regressions. The
  dedicated `qsort-edge-wave1200-audit.py` campaign runs 48 stack/no-stack and
  peep/nopeep runtime controls across 12 source variants, preserves exact
  selection for baseline and renamed functions, proves generic fallback for
  volatile and alternate-width arrays, changed array extent, volatile and
  unsigned loop state, alternate comparator/failure identities, a second
  array, and extra CFG, and rejects all 561 type, width, stride/immediate,
  operand, and identity mutations with independently forced
  `spilled-phi-slot` fallback. The clean selected hash is `7564150d` and
  assembly SHA-256 is
  `a55e6f4cb97df1564115bc1f3fc1b1790141469b4dbff8f706dd5127f6ef4113`.
  The standalone audit, all 136 Python script tests, and both strict 506-app
  release modes pass. The broader coverage objective remains incomplete.
- Wave 1300 revisits the already-hardened
  `mir_match_fixed_softmax_schedule` proof with a dedicated modern fixture and
  forced-fallback audit. The earlier Wave 22 campaign had already reduced its
  broad 2,479-mutation census to zero meaningful survivors, but retained
  benign mutations of inactive MIR metadata and did not independently force
  the named generic candidate for every semantic rejection. New
  `tests/mir-clobber/fixsmx.c` isolates the 154-instruction, 13-block fixed
  kernel and checks four asymmetric input vectors, guard words, and an
  independent fixed-point oracle.
  `fixed-softmax-wave1300-audit.py` runs 72 stack/no-stack and peep/nopeep
  runtime controls across 18 source variants. It retains exact selection for
  baseline, renamed-function, renamed-clamp, prefix-increment, and oversized
  table forms; proves generic fallback for volatile vector/table/local state,
  alternate count/weight types, indirect or variadic clamp calls, changed
  scale and clamp limits, non-void return, undersized table, subtraction, and
  extra CFG; and rejects 278/278 matcher-relevant type, width, constant/index,
  operand-dataflow, and storage-identity mutations. Every mutation is
  independently reproduced with
  `DCC_MIR_SELECT_CANDIDATE=spilled-phi-slot`, with zero meaningful survivors.
  Current production code rejected all controls, so this found proof gaps only
  and no genuine false acceptance. The clean selected hash remains `cf58a4d7`
  and assembly SHA-256 is
  `c9c86c97091c14451080c8a64ff0a0fcec70fe664049cd7a6dc0fc058568b954`.
  The broader coverage objective remains incomplete.
- Wave 1600 closes the historical `mir_match_board_search_schedule` proof gap
  and fixes genuine exact-schedule false acceptances. The old matcher proved
  the 215-instruction/25-block opcode and selected-edge outline, four signed
  word parameter offsets, helper names and argument counts, four global
  identities, a few strides, and three constants. It did not bind the complete
  SSA payload, scalar and pointer types, memory widths and flags, local
  identities, operators, PHI inputs, branch conditions, call ABI, or full
  global layouts. It accepted 311 of 359 targeted MIR mutations: 84/88 type,
  22/24 width, 78/78 first-operand, 30/30 second-operand, 41/61
  constant/operator/index, and 56/78 storage/call identity changes. It also
  selected the hard-coded schedule for signed-index and unsigned-check source
  near matches.
  The matcher now fingerprints every numeric MIR payload and CFG field, checks
  the complete function/helper ABI and observable identities, and proves the
  exact `movecnt`, `moves`,
  `side`, and `best_root` layouts and volatility properties. New
  `tests/mir-clobber/bsearch.c` isolates the recursive alpha-beta search and
  checks terminal, no-move, recursive, best-root, side-restoration, and score
  results. `board-search-wave1600-audit.py` runs 64 stack/no-stack and
  peep/nopeep runtime controls across 16 source variants, preserves exact
  selection for baseline and renamed functions, proves generic fallback for
  volatile state, alternate count/move/index/return/helper types, changed
  extents, extra CFG, and all 359 targeted MIR mutations, and independently
  forces `spilled-phi-slot` for every mutation. Zero meaningful survivors
  remain. The clean selected hash is `93b87cc0` and assembly SHA-256 is
  `42754918a399eb67099c355ace0d6c3f12fffa609d93741ab46c61fe943a3627`.
  The standalone audit, full Python script-test suite, and both strict
  506-app release modes pass. The broader coverage objective remains
  incomplete.
- Wave 1700 closes the remaining focused proof gap for
  `mir_match_scope_block_runner` without changing production code. The
  pre-existing `test-mir-scope-block-mutations.ps1` diagnostic covered four
  controls and 27 selected mutations against the broad `tforblk` application,
  but had no isolated fixture, forced named fallback, runtime matrix, or
  exhaustive field census. New `tests/mir-clobber/scopblk.c` isolates the
  698-instruction, 28-block schedule and retains runtime oracles for block
  shadowing, loop scope, static-local identity, helper results, summaries, and
  the final return.
  `scope-block-wave1700-audit.py` runs 64 stack/no-stack and peep/nopeep
  runtime controls across 16 source variants, proves the clean exact schedule,
  and proves generic fallback for volatile global/long state, changed local
  widths and operators, alternate loop CFG, check/parameter/helper ABI and
  identity changes, non-static helpers, duplicate calls, and summary-string
  aliasing. It rejects 3,810/3,810 matcher-relevant mutations: 698 each of
  type, memory width, first operand, second operand, and immediate/index
  fields, plus all 320 meaningful storage/call identities. Every rejection is
  independently reproduced with
  `DCC_MIR_SELECT_CANDIDATE=spilled-phi-slot`, with zero meaningful survivors.
  Current production code rejected every control, so this found proof gaps
  only and no genuine false acceptance. The clean selected hash is
  `b3ab4a13` and assembly SHA-256 is
  `47a208a38c9e04f30ca47e8b5ab9c28aabe5d3362e3524c42cdd565d69dccce9`.
  The broader coverage objective remains incomplete.
- Wave 1800 closes the historical `mir_match_global_array_fma` proof gap and
  fixes genuine exact-schedule false acceptances. The old matcher checked all
  24 opcodes, but only 17 instructions had any payload checks; it did not prove
  the global/index/load arithmetic types, two indexed widths, repeated global
  offsets, promoted operand identities, binary secondary types, or memory and
  bitfield flags. It accepted 20 of 58 representative mutations: 10/14 type,
  2/7 width, 2/7 immediate/offset, and 6/13 storage-identity changes.
  The matcher now binds all three addresses to the same nonvolatile global
  float array, proves every emitted index/load/store/binary type and width,
  checks the complete FMA dataflow, and verifies promoted operand identities
  plus zero memory, qualifier, and bitfield flags. The schedule has one block
  and no PHIs, so there are no internal CFG edges or PHI predecessors to
  mutate.
  New `tests/mir-clobber/gafma.c` isolates the 24-instruction schedule and
  checks four independently tabulated products/addends, stored results, and
  array guards. `global-array-fma-wave1800-audit.py` runs 24 stack/no-stack
  and peep/nopeep runtime controls across six source variants, preserves exact
  selection for baseline, renamed, and unsigned-index forms, proves generic
  fallback for volatile-array, narrow-index, and extra-CFG near matches, and
  rejects all 58 matcher-relevant type, width, operand, operator/offset, and
  identity mutations. Every mutation independently selects forced
  `spilled-phi-slot` fallback, with zero meaningful survivors. The clean
  selected hash is `68dc3c28` and assembly SHA-256 is
  `5bebfa8ada44767c744c777b6113c6c254ae84a3a8fde6f8bde2c26a963e7dfe`.
- Wave 1900 closes the historical `mir_match_wide_hash33` proof gap and fixes
  severe exact-schedule false acceptance. The old 32-instruction matcher
  explicitly constrained only 19 instruction positions and left complete
  opcode, destination, type, CFG-edge, PHI-predecessor, qualifier, memory-flag,
  bitfield, and metadata coverage unproved. Exhaustive mutation found that it
  accepted 687 of 746 per-instruction field and storage-identity changes.
  The matcher now verifies the complete 23-field payload of every instruction,
  including both CFG successors and PHI predecessors, and explicitly binds all
  parameter loads and the pointer update to the same parameter location.
  Source identifier spelling remains irrelevant.
  New `tests/mir-clobber/whash33.c` isolates the schedule and independently
  checks five tabulated 32-bit hash results, including high-bit bytes. New
  `wide-hash33-wave1900-audit.py` builds an isolated diagnostic mutation
  compiler, runs 20 stack/no-stack and peep/nopeep controls across baseline,
  renamed-function, renamed-local, volatile, and alternate-CFG forms, and
  rejects all 746 mutations across every instruction and payload field. Every
  mutation independently selects forced `spilled-phi-slot` fallback. The clean
  selected hash is `4fd89752` and assembly SHA-256 is
  `bf0ced071ba71d9e4ff2a9f109a80ef9d2fa1d1f925e285ceeaad241f0a11ec0`.
  Zero meaningful survivors remain. The standalone audit, all 136 Python
  script tests, and both strict 506-app release modes pass. The broader
  coverage objective remains incomplete.
- Wave 2000 closes the historical `mir_match_fortran_grow_schedule` proof gap
  and fixes genuine exact-schedule false acceptances. The old 92-instruction,
  11-block matcher bound one parameter, two globals, three callees, two string
  IDs, and four positive constants, but did not prove the instruction
  opcodes, types, widths, SSA operands, operators, memory flags, complete CFG,
  call identities, or string contents. It falsely accepted 476 of 489
  matcher-relevant diagnostic mutations: 92/92 type, 92/92 width, 90/92 first
  operand, 92/92 second operand, 92/92 immediate/operator, and 18/29 identity
  changes. A fill-byte source near match was also selected and demonstrably
  emitted zero-filled memory instead of the requested byte value.
  The matcher now fingerprints every numeric MIR payload, CFG, object,
  declaration, and function property; separately binds all emitted globals
  and callees; and validates both failure strings. New
  `tests/mir-clobber/fortgrow.c` independently checks no-growth, small-step,
  large-step, clamp, preservation, and fill behavior.
  `fortran-grow-wave2000-audit.py` runs 28 stack/no-stack and peep/nopeep
  runtime controls across seven source variants and rejects all 2,145
  mutations: all 23 fingerprinted fields at every instruction plus 29
  meaningful storage/call identities. Every mutation independently selects
  forced `spilled-phi-slot` fallback, with zero survivors. The clean selected
  hash is
  `41574932` and assembly SHA-256 is
  `b45cda486ecdd0330ca0784e70fdcadc20edcd5d5418a94751305c325b593e59`.
  The broader coverage objective remains incomplete.
- Wave 2300 closes the historical `mir_match_variadic_join_report` proof gap
  and fixes severe exact-schedule false acceptance. The old 63-instruction,
  five-block matcher touched 41 instruction positions but fully verified none:
  it checked selected opcodes, constants, argument relationships, and branch
  labels while leaving most types, destinations, operands, widths, memory
  flags, CFG successors, PHI predecessors, object identities, qualifiers, and
  instruction metadata unproved. Exhaustive mutation found that it falsely
  accepted 1,366 of 1,470 changes, including all 63 type, memory-width,
  memory-flag, qualifier, bitfield, object, successor, and PHI-predecessor
  mutations.
  The matcher now fingerprints all 23 numeric semantic and structural fields
  on every instruction, canonical source-name identity relationships, and both
  emitted callee contracts. New `tests/mir-clobber/varjoin.c` isolates the
  schedule and independently derives the expected joined length, separator
  count, and complete output text. New
  `variadic-join-report-wave2300-audit.py` builds an isolated diagnostic
  mutation compiler, runs 24 stack/no-stack and peep/nopeep controls across
  six source variants, and rejects all 1,470 mutations with generic fallback
  and zero survivors. Every rejection independently selects forced
  `spilled-phi-slot` fallback. The clean selected hash is `c6508ce3` and
  assembly SHA-256 is
  `ff651adf1097397693e62cb00814dd669cd2ee87301952f61204d2812214a1b9`.
  The standalone audit, all 136 Python script tests, and both strict 506-app
  release modes pass. The broader coverage objective remains incomplete.
- Wave 2400 closes the historical `mir_match_list_reverse_schedule` proof gap
  and fixes severe exact-schedule false acceptance. Although the old matcher
  checked all 39 opcodes, it explicitly related fields on only 15 instruction
  positions and omitted most destinations, types, operands, immediates, memory
  attributes, CFG successors, PHI predecessors, and auxiliary metadata.
  Exhaustive mutation found that 814 of 897 per-instruction field changes were
  falsely accepted. The matcher now fingerprints all 23 numeric semantic and
  structural fields on every instruction. Struct type IDs are normalized while
  retaining type kind, pointer depth, width flags, and the binary operand type,
  so equivalent declarations in different source contexts remain accepted.
  New `tests/mir-clobber/listrev.c` independently checks node identity, six
  reversed values, termination, and a tabulated rolling checksum. New
  `list-reverse-wave2400-audit.py` runs 20 stack/no-stack and peep/nopeep
  controls across baseline, renamed-function, renamed-local, volatile-member,
  and extra-CFG variants, and rejects all 897 mutations with generic fallback.
  Every rejection independently selects forced `spilled-phi-slot` fallback.
  The clean selected hash is `3b227728` and assembly SHA-256 is
  `c9140e698d415e08aac3a9a9eae20d149a44dbd7b170fdac455be0c73cfbd508`.
  Zero meaningful survivors remain. The standalone audit, all 136 Python
  script tests, and both strict 506-app release modes pass. The broader
  coverage objective remains incomplete.
- Wave 2600 replaces the partial recursive-byte MinMax evidence with a complete
  exact-schedule proof and fixes severe false acceptance in
  `mir_match_recursive_byte_minimax_schedule`. Although the old matcher checked
  all 253 opcodes, selected control edges, constants, locations, calls, and
  dataflow, it did not bind most instruction destinations, types, operands,
  CFG successors, PHI predecessors, memory/qualifier/bitfield state, or
  auxiliary metadata. It falsely accepted 4,899 of 5,857 exhaustive
  per-instruction and storage-identity mutations.
  The matcher now fingerprints 23 semantic and structural fields on every
  instruction, including both CFG successors and PHI predecessors, while its
  existing symbol and location checks preserve source-name independence.
  New `tests/mir-clobber/bminimax.c` independently computes the recursive
  alpha-beta oracle for three initial positions and checks the exact result,
  move count, restored board, and aggregate signature. New
  `recursive-byte-minimax-wave2600-audit.py` builds an isolated diagnostic
  mutation compiler, runs 40 stack/no-stack and peep/nopeep controls across ten
  source variants, and rejects all 5,857 mutations across all 253 instructions,
  all 23 fingerprinted fields, and 38 meaningful identities. Every mutation
  retains verified generic fallback, including forced `spilled-phi-slot`
  selection. The clean selected hash is `8b08568e` and assembly SHA-256 is
  `8b677c720df6a1ae17b3236fd665e83bc4e786532262b3fa5e6456be43a43d4f`.
  Zero meaningful survivors remain. The standalone audit, all 136 Python
  script tests, and both strict 506-app release modes pass. The broader
  coverage objective remains incomplete.
- Wave 2800 closes the historical `mir_match_allocator_stress_schedule` proof
  gap and fixes severe exact-schedule false acceptance. The old
  444-instruction, 32-block matcher directly inspected only 51 instruction
  positions: 26 calls, three initial global references, ten string addresses,
  and twelve constants. It did not verify the complete opcode, type, SSA,
  operator, memory/qualifier, CFG-edge, PHI-predecessor, or metadata payload,
  nor did it bind 28 later array addresses to the selected globals. It falsely
  accepted 10,100 of 10,267 exhaustive mutations. This included every type,
  memory-size, CFG-successor, PHI-predecessor, object, qualifier, bitfield, and
  metadata mutation, plus 362/444 opcode and 26/55 storage/call-identity
  mutations. A source near match changing the pattern offset from 11 to 12
  also selected the exact schedule and failed its independent runtime oracle.
  The matcher now fingerprints all 23 numeric fields of every instruction and
  explicitly binds all 18 slot-array and ten size-array addresses.
  New `tests/mir-clobber/alstress.c` independently simulates the deterministic
  allocation-state transitions and checks helper counts, final RNG state, and
  the accumulated pattern-slot checksum.
  `allocator-stress-wave2800-audit.py` compiles 16 stack/no-stack and
  peep/nopeep controls, runs the eight no-stack binaries (the deliberate
  heap-filling workload cannot coexist with the stack-collision guard), and
  rejects all 10,267 mutations with generic fallback and zero survivors. The
  clean stack-check selected hash is `f37f5f2e` and assembly SHA-256 is
  `d804868b0929fc88e2f70faf045ae2143f64249972aaf434b4f625afaf26b276`.
  The standalone audit, all 136 Python script tests, and both strict 506-app
  release modes pass.
  The broader coverage objective remains incomplete.
- Wave 3200 closes the historical
  `mir_match_anonymous_initializer_report_schedule` proof gap and fixes a broad
  exact-schedule false-acceptance class. The old 278-instruction matcher checked
  every opcode, but only 117 instructions appeared in its remaining explicit
  field and relationship checks; 5,796 of 6,459 exhaustive relevant field and
  storage-identity mutations retained the hard-coded report before the fix.
  The matcher now fingerprints all 23 semantic and structural instruction
  fields, binds all six initialized local aggregates to every later root
  address, and proves the print, numeric-check, string-check, and failure-global
  ABI/type contracts. New `tests/mir-clobber/anoninit.c` independently checks
  every initialized bitfield, union member, nested anonymous aggregate, string,
  and final result. New
  `anonymous-initializer-report-wave3200-audit.py` builds an isolated diagnostic
  mutation compiler, runs 20 stack/no-stack and peep/nopeep controls across five
  source variants, and rejects all 6,459 mutations across all 278 instructions,
  all 23 fingerprinted fields, and 65 meaningful identities. Every rejection
  retains verified generic fallback, including forced `spilled-phi-slot`
  selection. The clean selected hash remains `a893ab33` and assembly SHA-256 is
  `9e373331e798ac87fc5d2d6a1de99aec32aa54ab159ac5a6857e06264e552a7d`.
  Zero meaningful survivors remain. The standalone audit, all 136 Python
  script tests, and both strict 506-app release modes pass. The broader
  coverage objective remains incomplete.
- Wave 4000 closes the historical `mir_match_pi_digit_schedule` proof gap and
  fixes severe exact-schedule false acceptance. The 76-instruction matcher
  directly referenced only 27 instruction positions and did not prove the
  complete opcode, type, SSA, memory/qualifier, CFG-edge, PHI-predecessor, or
  metadata payload. The old matcher accepted 1,642 of 1,754 exhaustive
  per-instruction field and call-identity mutations (93.6%). The matcher now
  fingerprints all 23 semantic and structural fields on every instruction and
  validates that the emitted assertion string is a valid narrow literal with
  the required expression prefix. New `tests/mir-clobber/pidigit.c` computes
  the first eight hexadecimal digits of pi and checks them against the
  independent `243f6a88` oracle. New `pi-digit-wave4000-audit.py` builds an
  isolated diagnostic mutation compiler, runs 12 stack/no-stack and
  peep/nopeep controls across exact, renamed, and reordered-bound variants,
  and rejects all 1,755 mutations with verified generic fallback and zero
  survivors. The clean stack-check selected hash is `050cebd0` and assembly
  SHA-256 is
  `0481fbb52fa311e135a15dddcaee64cea71940af5999f07dbbe80da26d935a5f`.
  The standalone audit, all 136 Python script tests, and both strict 506-app
  release modes pass. No separate clobber manifest was needed. The broader
  coverage objective remains incomplete.
- Wave 4100 closes the historical `mir_match_float_sweep_schedule` proof gap
  and fixes severe exact-schedule false acceptance. The 431-instruction
  comparison variant directly classified only its 38 stores and 19 calls;
  most opcode, type, SSA, memory/qualifier, CFG-edge, PHI-predecessor, and
  metadata fields were not proved. The old matcher accepted 8,745 of 9,970
  exhaustive per-instruction field and meaningful identity mutations (87.7%).
  The matcher now fingerprints all 23 semantic and structural fields on every
  instruction before retaining either current 431-instruction comparison
  shape or the 434-instruction runtime-library shape; the previously admitted
  but unproved 440-instruction legacy outline now falls back conservatively.
  New `tests/mir-clobber/fltsweep.c` isolates the comparison schedule and
  checks eleven library sine results against an independently implemented
  Taylor-series oracle. New `float-sweep-wave4100-audit.py` builds an isolated
  diagnostic mutation compiler, runs 16 stack/no-stack and peep/nopeep
  controls across exact, renamed, volatile, and extra-CFG variants, executes
  all eight no-stack binaries, and rejects all 9,970 mutations with verified
  generic fallback and zero survivors. The clean stack-check selected hash is
  `2a78fb3a` and assembly SHA-256 is
  `74674d626ad7b36e43e807e1f1f421a3ae5972aacf8acca749a8e1ef38c82827`.
  No separate clobber manifest was needed. The standalone audit, full Python
  script-test suite, and both strict 506-app release modes pass. The broader
  coverage objective remains incomplete.
- Wave 6000 closes the historical `mir_match_board_attack_schedule` proof gap
  and fixes severe exact-schedule false acceptance. The old 676-instruction,
  118-block matcher checked every opcode and 98 selected control edges, but
  directly referenced only 82 instruction positions in its remaining field,
  symbol, constant, and call checks. It left most destination/type/operand,
  memory/qualifier, CFG-successor, PHI-predecessor, object, bitfield, and
  auxiliary metadata unproved. Exhaustive mutation found 14,401 false
  acceptances among 15,548 field changes (92.62%); 32 of those invalid
  schedules crashed during exact emission.
  The matcher now fingerprints all 23 numeric semantic and structural fields
  on every instruction plus object, declared-local, alias, and whole-function
  metadata, while preserving source-name independence and the existing
  explicit board, direction-array, parameter, constant, call, and ABI proofs.
  New `tests/mir-clobber/bdattack.c` compares pawn, knight, slider, blocked-ray,
  king, and empty-board results against an independent row/column oracle. New
  `board-attack-wave6000-audit.py` builds an isolated diagnostic mutation
  compiler, runs 20 stack/no-stack and peep/nopeep controls across baseline,
  renamed-function, volatile-board, volatile-direction, and extra-CFG variants,
  and rejects all 15,548 mutations with generic fallback and zero survivors.
  The clean selected hash remains `7e3a2471` and assembly SHA-256 is
  `fae46f9c6ebbd1d2cc1dc6af6b716c607e85cc1d06b502bb7f61c3a104fde630`.
  The standalone audit, all 136 Python script tests, and both strict 506-app
  release modes pass. The broader coverage objective remains incomplete.
- All eight push/PR checks for the PR #193 implementation passed: Linux,
  macOS, Windows, and the no-PowerShell build in both event runs.
- Successful runs: `34192914081` and `34192909889`.
- Main CI run `34193712407` passed for the merge commit. The two handoff-only
  commits also passed all four jobs in run `34193879724` before being carried
  onto the continuation branch.
- All eight push/PR checks passed for continuation head
  `411c1e91e824b64c063d6ebd337a9a105ce47813`: Linux, macOS, Windows, and
  the no-PowerShell build in runs `34201821020` and `34201817489`.
- PR #194 was clean and mergeable at that head. The user subsequently directed
  future increments to be fully verified locally, pushed, and not held waiting
  for GitHub Actions. This CI-evidence-only handoff update is therefore pushed
  without awaiting its workflow; PR #194 remains open and no merge is claimed.

The previous machine named the repository remote `upstream`; a fresh clone
normally calls it `origin`. Inspect remotes and adapt commands below.

```sh
git status --short --branch
git remote -v
gh pr view 193 --repo davidly/dcc --json state,headRefOid,mergeCommit,statusCheckRollup
gh pr checks 193 --repo davidly/dcc
```

PR #193 needs no further action. For a continuation PR, finish the current
head's CI first. Diagnose failures with
`gh run view RUN_ID --repo davidly/dcc --log-failed`; repair the actual problem,
validate, commit, push, and wait again. Once all checks pass:

```sh
gh pr checks PR --repo davidly/dcc --watch --interval 30
gh pr view PR --repo davidly/dcc --json headRefOid
gh pr merge PR --repo davidly/dcc --merge --match-head-commit VALIDATED_HEAD_SHA
git fetch origin
gh pr view 193 --repo davidly/dcc --json state,mergedAt,mergeCommit,url
git diff --exit-code VALIDATED_HEAD_SHA origin/main
```

Substitute the real PR, remote, and validated SHA. If main acquired unrelated
work, investigate tree differences instead of resetting. Coordinate ownership:
the old VS Code session and CLI must not both edit/publish the branch. No old
terminal watcher needs to be migrated.

## New Machine Setup

Install Git, authenticated GitHub CLI for publication, a native C/C++ toolchain,
CMake, PowerShell 7, and Python 3. Coverage additionally needs Clang and compatible
`llvm-profdata`/`llvm-cov`. On Windows use Visual Studio C++ Build Tools. Rebuild
native binaries; never copy CMake caches or executables between architectures.

```sh
gh repo clone davidly/dcc
cd dcc
git submodule update --init --recursive
git fetch origin
git switch --track origin/test/ast-mir-integrated
```

If the branch was deleted after merge, use main. In an existing checkout,
inspect local changes before switching and do not discard user work.

Clone <https://github.com/davidly/ntvcm> alongside dcc and build the emulator:
its Linux build script is `m.sh`, and macOS uses `mmac.sh`. The exact Windows
compiler setup/build and all CI dependencies are in the
[CI workflow](../.github/workflows/ci.yml). Put the dcc root and the native
ntvcm executable's directory on PATH. For a POSIX shell in dcc:

```sh
export PATH="$PWD:$PWD/../ntvcm:$PATH"
pwsh ./scripts/build-dcc.ps1
```

This builds dcc, dccpeep, dccrtlstrip, dccmake, m80c, l80c, dcc-debug-host, and
the example debugger I/O adapter. The [no-PowerShell build](../m-posix.sh) is
also supported but does not replace the primary PowerShell validation matrix.

Read these repository skills directly even if the CLI does not discover them:

- [Toolchain workflow](../.github/skills/dcc-project/SKILL.md).
- [Generated MIR contracts](../.github/skills/mir-migration/SKILL.md).
- [Target C applications](../.github/skills/dcc-cpm-z80/SKILL.md).

## Architecture and Constraints

```text
dcc -> dccpeep (optional) -> m80c -> dccrtlstrip -> m80c -> l80c -> ntvcm
```

dcc is a host compiler targeting Z80 CP/M 2.2. Every production function body
comes from verified, generated MIR. Post-parse AST processing is metadata-only.
Generic fallback means a generated homed/spilled MIR emitter, NOT a legacy AST
body emitter. Do not restore legacy emission, replay, or allocation retries.

- Target int/short/pointer/size_t are 16-bit; long/float are 32-bit; char is
  signed. No double or long long. C89 is the base with selected C99/C11 features.
  A host-C oracle must explicitly model target conversions and avoid UB.
- BC/DE are caller-saved, IY callee-saved. PHIs use values on edges. Arguments
  stay live through their matching call-site ID, not just their MIR_ARG record.
- Unknown aliases, volatility, calls, and opaque assembly invalidate proofs
  conservatively. Declined selectors must not contaminate later output.
- Keep semantic legality separate from profitability. Never select production
  code based on app/function names, legacy output, or baseline values.
- Correct output that regresses checked peep or nopeep performance is unfinished.
  Do not change baselines to hide regressions.
- Preserve full `-g` and release-identical `-gline` metadata and debugging.
  dcc-debug-host owns source debugging; ntvcm runs ordinary binaries.

## History to Review

Use `git show COMMIT` or `gh pr view NUMBER --repo davidly/dcc` as needed.

| Commit/PR | Relevance |
| --- | --- |
| PR #184 | Independent dominance verification and entry-definition safety. |
| PRs #187-#191 | Integrated coverage and indirect/deep callable metadata stack. |
| PR #190 | Mixed AST production/legacy function classification. |
| PR #191 | Abstract/deep function-pointer signatures and indirect results. |
| PR #192; main merge `a39decf2` | Independent generic MinMax peephole fix. |
| `c1f278b7` | Bounded signed byte-test auxiliary-effect proofs. |
| `9689e622` | Validated integration checkpoint of original stacks. |
| `d9e8e3aa` | Seeded fuzzing, coverage ledger, mutation infrastructure, cache fix. |
| `c21492f7` | Call arity verification and independently clean mutant builds. |
| `38da675e` | Correct successful exit after expected native mutation failures. |

The original MinMax defect: forced `spilled-all` produced 43,854 moves instead
of 6,493 for one iteration. A byte zero-test peephole removed a zero-extension
while HL remained live, after another pass removed a later `ld h,0` as redundant.
The fix proves HL dead and, for signed forms, proves altered A/parity effects
unobservable with bounded conservative CFG analysis. An exact schedule passing
must never substitute for validating the generic path independently.

## Completed Follow-up

### Compiler Fixes

Seed 23117 found stale definition-cache state inside `mir_promote_objects` in
[dcc_mir.c](../src/dcc/dcc_mir.c). The pass queries definitions while destructively
rewriting them, so invalidation only after returning was too late. The fix
invalidates after removed OBJECT_MERGE/load definitions and after alias rewrites.
There are three invalidation sites, deliberately checked by the mutation runner.

`mir_verify_structure` now validates contiguous argument positions and known
prototype arity: exact counts for fixed prototypes and the fixed prefix for
variadic ones. Global/direct callees and named indirect loads/parameters use
available declaration metadata. Local metadata now retains
`declared_proto_variadic` in [dcc_mir_internal.h](../src/dcc/dcc_mir_internal.h).

Unprototyped/unresolved callees get contiguous-position checks only. Casts,
fields, PHIs, and returned callable expressions still need explicit signature
transport in MIR before complete arity validation is possible. Do not infer a
prototype from an unrelated symbol or claim this contract is fully covered.

### Test Inventory

Default fuzz seeds: 23117, 1, 65535. Each generates 12 functions with 96 result
assertions across bounded unsigned arithmetic, arrays, aliased pointer calls,
loops, and joins. The PowerShell oracle masks target arithmetic/storage widths.
Twelve normal configurations per seed yield 3,456 checks; forced generic
configurations add 2,304 full-program checks. Only `fuzz0` is forced to
`spilled-baseline` or `spilled-all`. Corruption controls intentionally fail all
96 assertions and exit 1.

Strict named-function near-match rejection currently covers `structv`, `stringv`,
`floatv`, `bitfield`, and `callid`, not all schedule families. Host tests include
16 diamond field mutations, call-ID/type/arity cases, local callback metadata,
valid controls, and dominance count/dimension guards.

Four compiler mutants disable dominance, argument ABI, call arity, and promotion
cache invalidation. A kill requires the designated assertion/cache diagnostic;
build errors, unrelated crashes, and survivors are not successful kills. Every
mutant must be clean-built: rapid source rewrites previously reused stale objects
and gave misleading results.

## CLI Continuation Checkpoint

The first main-based continuation reproduced two verifier defects before
fixing them:

- an unprototyped local callback could inherit a differently prototyped global
  merely because both used the same spelling, causing valid target C to fail
  MIR ABI/arity verification; and
- malformed scalar or aggregate indirect-call MIR could omit its callee value.

`mir_resolve_call_prototype` now treats a known local declaration as the
authoritative identity even when it has no prototype. The shared resolver owns
both ABI-type and arity queries so those checks cannot diverge. `tfpshad` is the
permanent target reproducer and retains the differently prototyped global as a
valid control. Host tests cover fixed, variadic, unprototyped, shadowed-global,
and missing-callee cases.

The host verifier now directly inspects retained liveness data: PHI inputs are
live only on their matching incoming edges, and call arguments remain live
into but not after their call. The clean-build mutation inventory is nine:
dominance, argument ABI, call arity, indirect callee, callback identity, PHI
edge liveness, call-argument liveness, PHI-consumer forwarding, and promotion-
cache invalidation. All nine are killed only by their designated assertions or
cache diagnostic.

Exact-selector rejection tests now require the intended function to report the
named rejection, not select an exact schedule, and select a named generic
homed/hybrid/regional/spilled emitter. Harness negative controls reject
unrelated-function and exact-selection evidence. `iyexact` remains a positive
word-table schedule control; changing only its initial table index in `iynear`
rejects that schedule and executes correctly through generic code.

The target-aware generator chooses between compatible callbacks with different
aliasing writes. Its PowerShell oracle independently models 8-bit and 16-bit
stores, write-before-read alias effects, and 16-bit arithmetic. Forced generic
checks rotate across functions of both widths instead of only `fuzz0`.
Reproducibility, inventory, corrupted-oracle, peep/nopeep, stack/no-stack, and
generic-candidate controls all pass.

LLVM 18 exposed a coverage-tool compatibility defect: its native
`-show-functions` report includes the 115 manifest-classified legacy functions
despite the generated name allowlist. The analyzer now ignores only that exact
classified exclusion set while retaining hard failures for executed legacy
functions, unexpected/duplicate rows, missing selected functions, or overlap.
No denominator or classification changed.

Local validation for `d0a9ed82` passed:

- canonical and independent CMake builds;
- both strict full+extended gates: 506 apps, 482 passed and 24 documented
  skips per configuration, zero failures, zero checked performance regressions;
- full MIR clobber, lifetime, required-emission, and nine-mutant suites;
- ASan/UBSan host verifier plus focused real-source compiler probes;
- 82 repository script tests;
- 10 debugger-host tests and two line-debug tests; and
- fresh Linux Clang 18.1.3 coverage collection.

The fresh function-scoped coverage result is:

| Metric | Covered / total | Percent |
| --- | --- | --- |
| Lines | 167,624 / 190,636 | 87.93% |
| Native branch outcomes | 85,629 / 144,936 | 59.08% |
| Functions | 4,058 / 4,384 | 92.56% |
| Regions | 151,473 / 170,483 | 88.85% |

The raw ledger has 59,001 uncovered outcomes, one reviewed and 59,000
unreviewed, plus 326 unexecuted included functions. No exclusion was added.
The only new performance row is the measured new `tfpshad` workload; existing
baselines were not moved despite 21 reported improvements. The broader
objective remains incomplete.

The next coverage batch found that `runall.ps1` hard-coded the repository-root
compiler for diagnostics, so coverage builds did not execute the diagnostic
AST paths the coverage guide claimed. The runner now honors `DCC` for that
subprocess, and `compiler-coverage.sh` runs an assertion-backed AST dump test.
Direct MIR stream tests cover block read/write, short and zero-sized I/O,
relative/end seeks, copy, hash/file transfer, and error controls. Two new
diagnostic fixtures cover do-while and generic compound-statement rejection.

With those tests, coverage is 4,063/4,384 functions, 167,810/190,636 lines,
85,813/144,936 native branch outcomes, and 151,629/170,483 regions. The raw
ledger has 58,817 uncovered outcomes, one reviewed and 58,816 unreviewed, plus
321 unexecuted included functions. The work remains far from 100%.

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
peephole modes. Stack/no-stack censuses show only `tptrcnd.main` changed:
35,074 fewer assembly-text bytes and 3,538 fewer instructions. Checked peep
cycles improve by 32.39%, nopeep cycles by 34.19%, and existing baselines stay
unchanged.

The restored schedule executes 44 previously unexecuted included helpers.
Coverage reaches 4,197/4,385 functions, 170,069/190,675 lines,
86,483/144,952 native branch outcomes, and 152,851/170,503 regions. The raw
ledger has 58,175 uncovered outcomes, one reviewed and 58,174 unreviewed, plus
188 unexecuted included functions. The broader objective remains incomplete.

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
177 unexecuted included functions. The broader objective remains incomplete.

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
155 unexecuted included functions. The broader objective remains incomplete.

`tclit.check_value_literals` was two removed instructions stale. Updating its
fixed call indices and complete semantic payload fingerprint restores the
exact value-literal schedule; the existing corrupted-call-ID control still
rejects it and executes generic code. It improves peep/nopeep cycles by
11.70%/12.15% and sizes by 7.35%/10.00%.

Coverage reaches 4,236/4,388 functions, 171,124/190,839 lines,
86,958/145,026 native branch outcomes, and 153,774/170,590 regions. The raw
ledger has 57,774 uncovered outcomes, one reviewed and 57,773 unreviewed, plus
152 unexecuted included functions. The broader objective remains incomplete.

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
reviewed and 57,223 unreviewed, plus 130 unexecuted included functions. The
broader objective remains incomplete.

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

Mutation review found two exact-match false acceptances before publication.
Changing the errno fixture's `close(99)` to `close(98)` still emitted the
hardcoded 99; the matcher now proves the three previously omitted bad-descriptor
constants and the near match executes generically. More seriously, changing a
struct-value call from `proto_sum_pair(y)` to `proto_sum_pair(x)` retained the
exact schedule and its hardcoded `y` argument. The attempted 602-to-623
struct-value logical adapter was removed rather than adding another partial
proof. Current lowering therefore uses generic MIR for that historical shape
until every aggregate and scalar call argument is proven.

Making the immediate-PHI-consumer host test reachable exposed two transform
defects: the pass read `phi->dst` after clearing the PHI, and retained
instruction pointers across insertion/reallocation. The pass now captures the
PHI and consumer values before either mutation. The permanent test verifies
both predecessor consumer/return pairs and post-transform MIR validity; a ninth
clean-build mutant proves the assertion kills the original failure.

The corrected deterministic report is 4,338/4,393 functions,
175,101/190,992 lines, 88,685/145,086 native branch outcomes, and
157,056/170,662 regions. The raw ledger has 56,132 uncovered outcomes, one
reviewed and 56,131 unreviewed, plus 55 unexecuted included functions. The
function percentage is 98.75%; its decrease from the provisional 99.27%
measurement is the honest consequence of disabling 23 under-proven
struct-value helpers. The broader objective remains incomplete.

Local validation for `edfa976b` passed canonical and independent CMake builds,
the full clobber/lifetime/required-emission suites, 81 pointer mutations, all
nine clean-built compiler mutants, 82 Python tests, ASan/UBSan verification,
10 debugger-host tests plus two line-debug tests, runtime/module audits, and
both strict 506-app full+extended release gates with zero failures or
performance regressions. Stack and no-stack selector censuses retained all
3,039 generated functions; only the intended `tlimits` selection changed from
the parent checkpoint. A fresh LLVM 18 coverage run produced the totals above.
The commit is pushed per the user's local-validation workflow without waiting
for GitHub Actions.

The following generic-emitter increment raises coverage to 4,344/4,393
functions (98.88%), 175,275/190,997 lines (91.77%),
88,806/145,094 native branch outcomes (61.21%), and 157,261/170,670 regions
(92.14%). The raw ledger has 56,018 unreviewed outcomes and 49 unexecuted
functions.

New target controls force and execute constant/dynamic inline byte stores plus
an adjacent-byte regional call in all stack/peephole modes. A field-gap near
match proves generic fallback and absence of the paired marker under both normal
selection and an explicitly forced regional candidate. This permanently checks
that declining the adjacent-byte micro-optimization does not decline or
contaminate the valid regional stream. Direct host controls cover dense-switch
width state and both accepted/rejected spilled preflight paths. Candidate-matrix
probes verify the wide-narrow cache against
both `tlongopt` and `tm1mu.mulmod`.

Review found that wide-narrow cache verification overwrote the cached set
before comparison; it now compares preserved state before rebuilding. Review
also found and fixed selector inheritance in the paired near match and added a
positive preflight control. The two lazy-wide helpers are contradictory with
the one/two-byte lazy eligibility invariant, and the residual historical
spilled helpers remain in the denominator rather than being revived solely for
coverage.

Local validation for `05043bef` passed canonical/CMake builds, full MIR
clobber and candidate-matrix controls, nine clean-built mutants, 82 Python
tests, ASan/UBSan verification, both strict 506-app release gates, debugger
tests, and stack/no-stack censuses with all 3,039 functions retained and zero
production selector changes. It is pushed without waiting for GitHub Actions.

The next exact-runner increment raises coverage to 4,358/4,393 functions
(99.20%), 176,268/191,011 lines (92.28%), 89,330/145,104 branch outcomes
(61.56%), and 158,359/170,682 regions (92.78%). Thirty-five functions and
55,504 unreviewed outcomes remain.

The byte-math fixture covers all three helper functions with arithmetic,
logical, compare, decimal, and flag assertions; a compare-argument swap proves
generic fallback. The abort-file runner covers ten functions and accepts both
the historical 269-instruction MIR and current 264-instruction MIR. Current
lowering removes only the five instructions after the proven `noreturn`
`abort()` call, and the emitter now omits that unreachable print/return too.
An added-call near match rejects exact selection. Both strict release gates,
sanitizer probes, nine compiler mutants, and stack/no-stack censuses pass with
no production selector regressions.

The implementation commit is pushed without waiting for GitHub Actions, per
the user's local-validation workflow.

The lazy-wide/Fortran increment raises coverage to 4,361/4,393 functions
(99.27%), 176,575/191,181 lines (92.36%), 89,526/145,410 branch outcomes
(61.57%), and 158,725/170,990 regions (92.83%). Thirty-two functions and
55,614 unreviewed outcomes remain.

Lazy allocation now admits nonaggregate four-byte parameters; forced target
controls cover 32-bit return and call-argument paths. Seven production apps
adopt the candidate with no census or performance regression. The Fortran
fatal matcher now proves its complete print/range/index/exit dataflow after
review found same-shape false acceptances for exit status, output stream,
pointer subtraction order, and comparison operators. Six exact/current/mutated
source forms exercise the accepted template and generic rejections.

The remaining function list consists of 23 intentionally disabled,
under-proven struct-value helpers and nine stale/dead historical helpers.
They remain in the denominator; no schedule, guard, or classification was
removed to raise the percentage.

The user subsequently authorized justified deletion. The unsafe struct-value
schedule and six obsolete/superseded helper closures were removed, while
forward-attention and global fixed-byte-walk emitters gained exact/generic
target controls. The redundant `incoming == 0` PHI check was removed after
preserving entry-PHI and all predecessor/dominance validation.

Function coverage is now exact: 4,357/4,357 (100.00%). Lines are
176,589/189,955 (92.96%), branches 89,591/144,652 (61.94%), and regions
158,744/170,023 (93.37%). Therefore the overall coverage objective remains
open despite complete function coverage.

The user's later direction authorizes deleting code when deletion is
technically correct and preserves baselines. The unsafe struct-value schedule
has therefore been retired completely: it was still dispatchable, and its
historical matcher accepted a demonstrated swapped-argument miscompile. Five
generic aggregate source variants execute through spilled MIR across 28 target
configurations, including full/line debug. Parent/current assembly, selectors,
and no-stack peep/nopeep cycles and linked sizes are identical for `tstructv`.

Fresh coverage is 4,361/4,369 functions (99.82%),
176,545/190,652 lines (92.60%), 89,536/145,238 branch outcomes (61.65%), and
158,725/170,766 regions (92.95%). The source deletion removes 24 definitions,
529 lines, 172 branch outcomes, and 224 regions; it is not an exclusion.
Eight retained functions and 55,432 unreviewed raw outcomes remain.

Local validation for `470d6389` passed the full clobber suite, sanitizer
compiler probes, nine clean-built mutants, 82 Python tests, both strict
506-app release gates, and stack/no-stack censuses retaining all 3,039
generated functions. Seven apps changed candidate metrics as expected from
lazy-wide admission; all passed runtime and performance checks. The commit is
pushed without waiting for GitHub Actions.

## Useful Repository Assets

| Asset | Purpose |
| --- | --- |
| [Host tests](../tests/host/mir_verify.c) | Direct valid/invalid MIR invariant tests. |
| [Host guide](../tests/host/README.md) | Detailed replay commands and matrix contracts. |
| [Fuzz generator](../scripts/new-mir-fuzz-source.ps1) | Deterministic target-aware programs and oracle. |
| [Generator tests](../scripts/test-mir-fuzz-source.ps1) | Reproducibility and test inventory. |
| [Clobber runner](../scripts/run-mir-clobber-tests.ps1) | Debug/stack/peep/generic matrices and rejection controls. |
| [Clobber sources](../tests/mir-clobber) | Small permanent semantic regressions. |
| [Mutation runner](../scripts/run-mir-compiler-mutations.ps1) | Isolated baseline and nine compiler mutants. |
| [Coverage workflow](../scripts/compiler-coverage.sh) | Instrumented compiler plus host verifier. |
| [Coverage guide](compiler-coverage.md) | Measurements, accounting, exclusions, remaining gates. |
| [Module manifest](../scripts/ast-mir-coverage.tsv) | Architectural module classification. |
| [Function manifest](../scripts/ast-function-coverage.json) | Mixed AST definitions and guarded references. |
| [Coverage analyzer](../scripts/ast-function-coverage.py) | Verified totals, raw gaps, anchored review evidence. |
| [Review annotations](../scripts/ast-coverage-reviews.json) | Reviewed guard retained in totals. |
| [Coverage tests](../scripts/tests/test_ast_function_coverage.py) | Scope, deduplication, classification, review checks. |
| [Peephole owner](../src/dccpeep/peep_pass_once.c) | Independent generic MinMax liveness fix. |
| [Selector](../src/dcc/dcc_mir_select.c) | Candidate diagnostics and production selection. |
| [Dominance verifier](../src/dcc/dcc_mir_verify.c) | Independent reachable-CFG verification. |
| [Runtime](../DCCRTL.MAC) | Helper and calling-convention evidence. |
| [Test overrides](../tests/_test_overrides.json) | Args, stdin, fixtures, stack sizes, documented skips. |
| [Performance baselines](../tests/perf_baselines.csv) | Checked performance, not a tuning knob. |

For the final completion run, set `DCC_COVERAGE_REQUIRE_COMPLETE=1`; the
analyzer then fails unless functions, lines, native branch outcomes, and
regions each have `covered == total`. The gap JSON also records zero-count
source-region anchors. Reviews remain dispositions, not covered outcomes.

## September 9 Retained-Coverage Checkpoint

Implementation commit `c5fb1236` adds a target assignment matrix and direct
host AST support/rejection assertions. The target matrix covers long, float,
plain integer, pointer, multidimensional array, pointer-to-array, and struct
member assignments in stack/no-stack, peep/nopeep, full-debug, and line-debug
modes.

Adding full-corpus debug censuses found and reproduced a real generic-emitter
defect in `tfmadd`: a float multiply fused into `__fmaf` was still classified
as an independent wide helper handoff. Full debug initially rejected the
overlapping stack plan. Suppressing only the emitted handoff then produced
loads from unallocated frame slots and wrong target values. The final fix
rejects fused multiplies in the shared helper-consumer proof, so backend-slot
planning and emission use the same invariant. The permanent `fmadddbg` case
checks runtime values in all 12 stack/peep/debug combinations.

The coverage workflow now compiles all 482 runnable applications under `-g`
and `-gline`, with and without stack checks. It also canonicalizes relative
coverage build paths before setting `LLVM_PROFILE_FILE`. This prevents CTest
from placing the host verifier profile under its working directory and
silently omitting 93 host-only functions from the merged report.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,357 / 4,357 | 100.00% |
| Lines | 176,746 / 189,957 | 93.05% |
| Native branch outcomes | 89,766 / 144,654 | 62.06% |
| Regions | 158,885 / 170,026 | 93.45% |

Relative to the preceding exact-function report, this adds 156 covered lines,
142 covered branch outcomes, and 120 covered regions. Remaining debt is 13,211
lines, 54,888 native branch outcomes, 11,141 regions, and 54,641 raw unreviewed
outcomes. The exact overall objective is therefore still open.

Local validation passed:

- 616 clobber configurations and all four 3,039-function debug censuses;
- both strict 506-app full+extended release gates;
- ASan/UBSan verifier and full-debug `tfmadd` compiler probe;
- 10 debugger-host tests and both line-debug tests;
- 83 Python tests and all nine compiler mutants; and
- the frozen 482-app no-stack parent comparison with no cycle or size
  regressions.

Follow-up commit `338657aa` proves and removes six repeated long/float
assignment blocks that were structurally preempted by the common index-lvalue
type gate. Direct host tests preserve long/float multidimensional behavior and
cover pointer arrays, dereferenced pointer-to-array rows, computed pointer
expressions, multidimensional pointer elements, and pointer-valued members,
including rejection controls. Release and stack censuses are byte-identical
for all 3,039 functions, and all strict, sanitizer, mutation, debugger, and
frozen performance gates pass.

The resulting scoped report, after adding malformed-MIR spilled preflight,
exact byte-math near-mutations, and reusable exact-matcher field mutation, is
4,359/4,359 functions, 177,027/189,946 lines (93.20%),
90,863/144,518 native branch outcomes (62.87%), and 159,049/169,867 regions
(93.63%). The preflight matrix covers
invalid return/value widths, unsupported opcodes, unresolved memory, indirect
widths, direct/indirect call ABI failures, aggregate-call ABI failures, and
invalid `va_arg` offsets. Exact-shape, selector-rejection, and backend-slot
diagnostic modes are also covered. The raw ledger has 53,408 uncovered
outcomes. The byte-math variants alter mask, comparison, complement, addend,
overflow, and logical semantics; each rejects the named schedule and runs
generically across 32 target configurations.

The diagnostic MIR mutator temporarily changes one checked instruction field
during exact matching, restores the full instruction, and invalidates def-use
caches before generic fallback. Its parser rejects malformed, overflowing,
out-of-range, and unknown values consistently on Windows and POSIX hosts; the
clobber suite clears inherited mutation state between cases. One hundred
eighty-six log-series field mutations cover every named rejection group plus
each retained array/local identity, constant, type, width, and SSA operand
predicate while the original program runs generically. Both mutation helpers
have exact line, branch, and region coverage. One hundred forty-four field
mutations additionally cover byte-math parameter, mask, comparison, memory,
call, decimal, arithmetic, carry, overflow, logical, negative, zero-flag, and
SSA operand checks. A named multidimensional-array control plus 93 field
mutations covers every rejection family while preserving exact/generic
`t2darr` output. The narrowed div/mod control adds 125 field/SSA mutations and
preserves all 66 `tdmfuse` checks through generic selection. The recursive
MinMax control adds 79 mutations across its move, board, call, and search
proofs while preserving the one-iteration oracle. The Catalan driver adds 110
restored-MIR mutations and preserves its 100-digit output with the canonical
768-byte stack. The ctype/realloc schedule adds 32 mutations across allocation,
copy, resizing, preservation, byte checks, free, and final-result proofs. Prime
search adds 71 mutations across ABI, initialization, normalization, loops, and
reporting. One hundred thirty-two fixture-backed attention mutations preserve
all 14 accuracy checks through spilled generic fallback. The complete clobber
manifest now contains 4,576 configurations.

A second assignment review removes a preempted 2-D address branch, a
pointer-array result path already handled for plain assignment, and a member
fallback already owned by earlier member-pointer and member-array gates.
Direct multidimensional long/float and pointer rejection controls preserve the
live behavior. Stack/no-stack censuses remain byte-identical for all 3,039
functions, and missing lines, branch outcomes, and regions fall by another 27,
33, and 39.

The next increment should rank gaps only from selected functions in
`ast-mir-function-coverage.json`. Do not rank raw LLVM rows for classified
legacy helpers such as `gen_assign_ident_ast` or `gen_call_ast`. Continue with
retained AST support gates, generic spilled-emitter rejection paths, exact
matcher semantic near-mutations, and deterministic allocation/I/O failure
injection. Do not retry the disproven broad AST index-fallback deletion.

## Validation Commands

Run from the repo root after a successful native build. Examples use a POSIX
shell; PowerShell uses `$env:NAME` for environment variables.

Focused replay:

```sh
cmake -S src/dcc -B build/mir-tests -DDCC_BUILD_MIR_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/mir-tests --target mir-verify-test --config Debug --parallel
ctest --test-dir build/mir-tests -C Debug -R '^mir-verify$' --output-on-failure
pwsh scripts/test-mir-fuzz-source.ps1
pwsh scripts/run-mir-clobber-tests.ps1 -Cases fuzz,callid
pwsh scripts/run-mir-clobber-tests.ps1 -Cases minimax
pwsh scripts/run-mir-compiler-mutations.ps1
```

Both release gates are required before publishing compiler/runtime changes:

```sh
DCC_MIR_REQUIRE_COMPLETE=1 DCC_MIR_REQUIRE_EMIT=1 \
  pwsh scripts/runall.ps1 -Mode full -Extended -RunTimeout 30 -FailuresOnly
DCC_MIR_REQUIRE_COMPLETE=1 DCC_MIR_REQUIRE_EMIT=1 \
  pwsh scripts/runall.ps1 -Mode full -Extended -NoStackCheck -RunTimeout 30 -FailuresOnly
pwsh scripts/run-mir-clobber-tests.ps1
pwsh scripts/run-mir-lifetime-tests.ps1
pwsh scripts/test-mir-require-emit.ps1
python3 -m unittest discover -s scripts/tests -p 'test_*.py'
git diff --check
```

For CFG, liveness, allocation, cache, or ownership changes, on a supported
Clang/GCC host:

```sh
cmake -S src/dcc -B build/cmake-sanitize -DDCC_BUILD_MIR_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build/cmake-sanitize --target mir-verify-test --parallel
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/cmake-sanitize -R '^mir-verify$' --output-on-failure
```

For debugger contracts:

```sh
cmake -S src/dcc_debug_host -B build/dcc_debug_host_tests \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dcc_debug_host_tests --config Debug --parallel
ctest --test-dir build/dcc_debug_host_tests -C Debug --output-on-failure
python3 scripts/tests/test_line_debug.py
```

Recreate source coverage:

```sh
sh scripts/test-coverage-sources.sh
python3 scripts/ast-function-coverage.py
sh scripts/compiler-coverage.sh
```

Reports are generated under `build/compiler-coverage/report/`; principal outputs
are `ast-mir-function-summary.txt`, `ast-mir-function-coverage.json`, and
`ast-mir-gaps.json`. Mutation logs/results are in
`build/mir-compiler-mutations/`. Clobber failures retain replay sources/build
products in `build/mir-clobber-failure-<GUID>`. These artifacts are deliberately
uncommitted and must be regenerated on the new machine.

Useful diagnostic controls: `DCC_MIR_REPORT=1`, `DCC_MIR_SELECT_REPORT=1`,
`DCC_MIR_SELECT_REPORT_FUNCTION`, `DCC_MIR_MACHINE_REPORT=1`,
`DCC_MIR_MACHINE_FUNCTION`, `DCC_MIR_MACHINE_TEMPLATE`,
`DCC_MIR_CACHE_VERIFY=1`, `DCC_MIR_SELECT_FUNCTION`, and
`DCC_MIR_SELECT_CANDIDATE`. Production selection must remain
semantic/structural.

## Results and Limitations

The previous machine passed both strict full+extended gates: 505 main apps,
481 passed and 24 documented skips per configuration, zero failures and no
checked performance regressions. Full MIR clobber/lifetime/required-emission,
sanitizer verifier, 81 script tests, and 10 debugger-host tests passed. All four
clean-built mutants were killed. These are historical results, not substitutes
for validation after edits or on a different toolchain.

The historical aggregate below was measured after the fuzz/cache checkpoint,
before the call-arity and CLI continuation follow-ups. Use the newer CLI
checkpoint above for current totals:

| Metric | Covered / total | Percent |
| --- | --- | --- |
| Lines | 167,485 / 190,465 | 87.93% |
| Native branch outcomes | 85,714 / 145,239 | 59.02% |
| Functions | 4,057 / 4,383 | 92.56% |
| Regions | 162,426 / 182,854 | 88.83% |

The raw ledger contained 59,341 missing branch records, one reviewed and 59,340
unreviewed, plus 326 unexecuted included functions. Raw LLVM branch records are
NOT the native summary denominator. Mixed AST classification includes 171
production functions from 286 definitions and excludes 115 legacy-only ones;
none of those exclusions executed in that run. No new exclusion was introduced.

`mir_verify_dominance` historically reached 214/214 lines and 125/126 branch
outcomes. The remaining `incoming == 0` check was later removed as redundant
after justified deletion was authorized: only reachable non-entry PHI blocks
reach that point, and each has a reachable predecessor by construction. The
entry-PHI `start == 0` rejection and predecessor dominance checks remain. Do
not label other gaps unreachable by analogy or merely because tests miss them.

The coverage guide's older "uncommitted" and "remote CI not run" wording
describes earlier checkpoints. Use GitHub for current publication state and
regenerate coverage before making claims about the current tree.

## Remaining Work and Expected Outcomes

After publication, choose a small falsifiable correctness gap from current
source and a fresh ledger. High-value directions:

1. Prove the guarded late-PHI call-crossing allocation path independently,
   including the narrow DE and wide BC:IY exceptions and their boundary
   save/restore behavior. Keep ordinary calls excluded from those homes.
2. Extend near-match rejection and generic equivalence across more exact
   families. Assert rejection and selected fallback for the intended function,
   plus correct execution, not an unrelated selection marker elsewhere.
3. Expand the generated grammar beyond bounded unsigned arithmetic and current
   memory/call forms. Define an independent target-correct oracle; avoid UB.
4. Add compiler mutants for remaining alias/store cache invalidation and
   regional boundary contracts. Investigate survivors and require
   mutation-specific semantic failures.
5. Review uncovered outcomes and unexecuted functions. Add supported-source or
   malformed-IR assertions where meaningful; retain precise evidence for
   defensive/unreachable classifications and recheck exclusion guards.

Each increment should produce a permanent reproducer, root-cause fix if needed,
valid/invalid controls, focused validation, required release/debug/performance
gates, and an explicit account of remaining limitations. Re-run coverage after
additions using the same architectural accounting. For selector/cost changes,
follow the MIR skill's before/after census workflow and test every changed app.

Broader completion requires evidence-backed disposition of meaningful gaps,
not a rounded percentage or four mutation kills. Report source coverage and
mutation inventory separately from semantic confidence. Do not manufacture
100% by deleting guards or excluding difficult paths. Final outcomes should
name merged commits, tested platforms, coverage provenance, unresolved
contracts, and remaining skips/survivors.

## Operational Lessons

- Run a focused executable check immediately after an edit. Never trust ctest
  against an old binary after a failed build.
- GitHub's PowerShell wrapper propagates `$LASTEXITCODE`. Expected mutation
  failures must not leak as script failure after assertions pass. `38da675e`
  returns zero only after successful checks/cleanup; genuine throws still fail.
- Clean-build each mutant and require its specific diagnostic.
- CP/M filenames must be 8.3: use `fz23117.c`, not `fuzz23117.c`. The clobber
  runner normalizes comma-separated Cases and rejects unknown names.
- Direct ntvcm runs require hard timeouts and configured args/stdin. Linux:
  `timeout 30 ntvcm -p -s:0 ...`; macOS:
  `perl -e 'alarm 30; exec @ARGV' ntvcm -p -s:0 ...`. Never use `-s:50000000`.
  Interactive input waits are not automatically compiler hangs.
- If dcc rejects test source, also check with
  `clang -std=c11 -Wall -Wextra -pedantic`, accounting for documented target
  differences. Valid supported C should lead to a compiler fix, not a silent
  source workaround.
- In zsh do not use `path` or `status` as scratch variables. Run critical gates
  serially if concurrent terminal output becomes ambiguous.
- Preserve unrelated user changes. No destructive resets, force-pushes,
  performance-baseline manipulation, or coverage exclusions to hide failures.

## Suggested First CLI Request

> Read docs/ast-mir-cli-handoff.md and its linked toolchain/MIR skills. Inspect
> current PR #193 and repository state. Finish authorized publication only with
> green checks for the exact current head; if merged, start from current main.
> Then continue the documented AST/MIR correctness and coverage work in small,
> assertion-backed increments, preserving honest denominators and all release,
> debug, and performance contracts. Do not claim the broad objective complete
> from the existing checkpoint. No old chat or local build artifacts are
> available on this machine.
