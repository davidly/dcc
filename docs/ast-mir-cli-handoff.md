# AST/MIR Correctness: Copilot CLI Handoff

Snapshot: 2026-09-08. This handoff requires no prior chat history, VS Code
session, local memory, or existing build artifacts. GitHub and the current
checkout are authoritative if the snapshot becomes stale.

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

## Publication State

- Repository: <https://github.com/davidly/dcc>.
- Continuation branch: `test/ast-mir-correctness`.
- Continuation PR: <https://github.com/davidly/dcc/pull/194>.
- PR #193 was merged as
  `74079b980a282e966b99d878256f89f799b63a64` on 2026-09-08.
- Latest continuation implementation:
  `67d55170` (`test: mutate ctype realloc matcher`).
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
match proves generic fallback and absence of the paired marker. Direct host
controls cover dense-switch width state and both accepted/rejected spilled
preflight paths. Candidate-matrix probes verify the wide-narrow cache against
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
4,359/4,359 functions, 177,002/189,945 lines (93.19%),
90,654/144,518 native branch outcomes (62.73%), and 159,030/169,867 regions
(93.62%). The preflight matrix covers
invalid return/value widths, unsupported opcodes, unresolved memory, indirect
widths, direct/indirect call ABI failures, aggregate-call ABI failures, and
invalid `va_arg` offsets. Exact-shape, selector-rejection, and backend-slot
diagnostic modes are also covered. The raw ledger has 53,617 uncovered
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
copy, resizing, preservation, byte checks, free, and final-result proofs. The
complete clobber manifest now contains 3,760 configurations.

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
`DCC_MIR_CACHE_VERIFY=1`, `DCC_MIR_SELECT_FUNCTION`, and
`DCC_MIR_SELECT_CANDIDATE`. Production selection must remain semantic/structural.

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

1. Carry explicit callable signatures through MIR casts, fields, PHIs, and
   returned callable expressions. Add valid/malformed controls and preserve
   unprototyped behavior before broadening arity verification.
2. Extend near-match rejection and generic equivalence across more exact
   families. Assert rejection and selected fallback for the intended function,
   plus correct execution, not an unrelated selection marker elsewhere.
3. Expand the generated grammar beyond bounded unsigned arithmetic and current
   memory/call forms. Define an independent target-correct oracle; avoid UB.
4. Add compiler mutants for more PHI, clobber, spill, alias, and cache contracts.
   Investigate survivors and require mutation-specific semantic failures.
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
