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

The broader request is NOT complete. Hundreds of included functions and tens
of thousands of raw branch records still need investigation.

## Publication State

- Repository: <https://github.com/davidly/dcc>.
- Branch: `test/ast-mir-integrated`.
- PR: <https://github.com/davidly/dcc/pull/193>.
- Latest implementation: `38da675e9236e7791b748cae89bc6f203eac479a`.
- All eight push/PR checks for that implementation passed: Linux, macOS,
  Windows, and the no-PowerShell build in both event runs.
- Successful runs: `34192914081` and `34192909889`.
- This handoff is a subsequent documentation commit. Its publication triggers
  new CI, so the implementation run IDs do not validate the newer PR head.
- The user authorized push and merge. Merge only after all checks pass for the
  actual current head, without bypasses or force-pushing.

The previous machine named the repository remote `upstream`; a fresh clone
normally calls it `origin`. Inspect remotes and adapt commands below.

```sh
git status --short --branch
git remote -v
gh pr view 193 --repo davidly/dcc --json state,headRefOid,mergeCommit,statusCheckRollup
gh pr checks 193 --repo davidly/dcc
```

If open, finish the current head's CI first. Diagnose failures with
`gh run view RUN_ID --repo davidly/dcc --log-failed`; repair the actual problem,
validate, commit, push, and wait again. Once all checks pass:

```sh
gh pr checks 193 --repo davidly/dcc --watch --interval 30
gh pr view 193 --repo davidly/dcc --json headRefOid
gh pr merge 193 --repo davidly/dcc --merge --match-head-commit VALIDATED_HEAD_SHA
git fetch origin
gh pr view 193 --repo davidly/dcc --json state,mergedAt,mergeCommit,url
git diff --exit-code VALIDATED_HEAD_SHA origin/main
```

Substitute the real remote and validated SHA. If main acquired unrelated work,
investigate tree differences instead of resetting. If already merged, skip
publication and start further work from current main. Coordinate ownership:
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

## Useful Repository Assets

| Asset | Purpose |
| --- | --- |
| [Host tests](../tests/host/mir_verify.c) | Direct valid/invalid MIR invariant tests. |
| [Host guide](../tests/host/README.md) | Detailed replay commands and matrix contracts. |
| [Fuzz generator](../scripts/new-mir-fuzz-source.ps1) | Deterministic target-aware programs and oracle. |
| [Generator tests](../scripts/test-mir-fuzz-source.ps1) | Reproducibility and test inventory. |
| [Clobber runner](../scripts/run-mir-clobber-tests.ps1) | Debug/stack/peep/generic matrices and rejection controls. |
| [Clobber sources](../tests/mir-clobber) | Small permanent semantic regressions. |
| [Mutation runner](../scripts/run-mir-compiler-mutations.ps1) | Isolated baseline and four compiler mutants. |
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

Last aggregate coverage was measured after the fuzz/cache checkpoint, BEFORE
the call-arity follow-up:

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

`mir_verify_dominance` reached 214/214 lines and 125/126 branch outcomes. The
remaining true outcome of `incoming == 0` has source-anchored reachability
evidence. Both guard and coverage outcome stay in the totals. Do not label
other gaps unreachable by analogy or simply because tests have not hit them.

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