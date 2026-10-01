# AST/MIR correctness: next-phase plan and cross-machine handoff

Prepared October 1, 2026 for `davidly/dcc`. This document is self-contained:
the next computer needs the repository, not this conversation, local worktrees,
session files, binaries, profiles, or remembered task state.

## 1. Authority, scope, and implementation model

Read this document completely, then load
[dcc-project](../.github/skills/dcc-project/SKILL.md) and
[mir-migration](../.github/skills/mir-migration/SKILL.md).
[The historical handoff](ast-mir-cli-handoff.md) and
[coverage ledger](compiler-coverage.md) provide supporting evidence, not an
instruction to resume an obsolete fleet, remove already-deleted code, or
republish an already-merged PR.

**Implementation model: GPT-6.1 Sol, model ID `gpt-6.1-sol`, reasoning effort
`medium`.** This supersedes the earlier GPT-5.6 Sol worker requirement.
Use this model and effort for the coordinator and all implementation workers.
The verified CLI launch options are:

```sh
copilot --model gpt-6.1-sol --reasoning-effort medium
```

When launching task agents, explicitly request `model: "gpt-6.1-sol"` and
`reasoning_effort: "medium"`. Selecting the parent model does not prove that
workers inherited it; inspect their actual model. Do not silently substitute
another model or effort if the requested configuration is unavailable.

**Compiler application/unit regression testing must use the new
`python3 scripts/runall.py` runner, not `runall.ps1`.** Run both strict full
stack/no-stack modes, including extended tests. Python proof-tool unit tests
remain `python3 -m unittest discover -s scripts/tests -p 'test_*.py'`;
specialized PowerShell proof harnesses are not replaced by the application
runner.

The goal remains honest, evidence-backed 100% coverage and stronger compiler
correctness. Passing tests, generated-MIR selection, raw field-mutation counts,
or deleting code do not establish that goal. This next phase has a bounded
deliverable: rehydrate the machine, close the Python aggregate-runner gap,
collect fresh coverage, complete up to four evidence-ranked proof increments,
and publish their exact-tree evidence. Stop at that phase boundary rather than
starting an endless matcher fleet.

The user authorizes local verification followed by commit/push without waiting
for GitHub Actions. Do not bypass branch protections, force-push, or perform a
new PR merge without authorization for that future phase. The merge authorized
with this planning document is separate from future implementation publication.

## 2. Persistent checkpoint and what is actually known

The outgoing branch is `test/ast-mir-proof-next`. Before this planning-only
commit, its published head was:

```text
28f3c5a61f5c9b8ac422f2e0964e83e4e44b1043
```

The final compiler/proof implementation checkpoint is `548964f9`; `28f3c5a6`
adds the registry and evidence documentation. The incoming main baseline was
`1dc57abd51449911c78652e459808a91beb02ac6`, including `scripts/runall.py`.
This handoff and those changes are intended to be merged into `origin/main`.
The publication PR is **#207**, titled `test/ast-mir-proof-next`, using that
existing branch. It was opened because GitHub reported no open PR for the
branch and its previous PR was already merged; the unrelated open #206 was
not used. Verify #207's actual merge commit and head on the next machine.
On the next machine, GitHub and fetched refs determine the actual merge state.
Require `28f3c5a6` to be an ancestor of the starting main; do not assume that a
branch with the old name still represents an open PR. PR #195 was the earlier,
already-merged legacy-removal PR, not this new handoff publication.

Production function bodies are generated from verified MIR only. The legacy
AST body emitters have already been removed. Remaining AST support is parsing,
classification, initializer capture, and metadata/debug support. There is no
legacy output oracle or fallback to restore.

### Completed proof phase

| Surface | Integrated commits | Representative mutations | Target controls |
| --- | --- | ---: | ---: |
| Status pack | `e4571346` | 1,897 | 42 |
| Recursive wide product | `67420f68`, K&R preservation `3273b215` | 462 | 88 |
| Recursive frame fill | `e8df60f1` | 1,222 | 42 |
| Byte rotate flags | `c16eafa7`, generic correction `548964f9` | 3,357 | 48 |

The total is 6,938 representative mutations and 220 target controls. The four
campaigns are registered in `scripts/compiler-coverage-campaigns.py`; its
unique campaign count is 107. This is an audit inventory, not a percentage of
all selectors or semantics.

Four active-MIR proof defects were reproduced: status conversions discarded
bits, a recursive comparison retained the wrong width, a displaced frame-fill
store retained the wrong sink address, and displaced rotate-local stores could
alias a parameter while exact emission omitted their writes. These reproductions
use diagnostic MIR mutations; natural-C reachability was not established.
Inactive/equivalent survivors are explicitly retained and classified.

A fifth defect **was source-reachable in defined C**: forwarding a promoted
boolean into a cast omitted its named byte home, which a later branch read.
Merged main produced 24/144 failures and checksum 2137218272 rather than the
independent masked target/Python oracle's 1692113144.
`548964f9` retains the home when additional uses exist while preserving HL
forwarding and the cast-only fast path. The discarded overwrite-value probe
was restored as a permanent control instead of hiding the failure.

### Local evidence from the outgoing machine

On the final implementation tree:

- Strict Python stack/no-stack full+extended regression gates: 483 passed,
  24 documented skips out of 507 apps; diagnostics, peephole fixtures, and
  extended execution passed. Checked stack-mode performance had no regressions.
- Both main-corpus MIR censuses: 3,063/3,063 generated functions; selections
  and reported output metrics/hashes matched the incoming main baseline.
- Extended census: 274/274 generated functions in each stack mode.
- Independent normal and ASan/UBSan MIR host suites: 5/5 each.
- Debugger-host suite: 10/10; focused full/line-debug and peep/nopeep controls
  were also checked by the implementation workers.
- Repository Python tests: 136 passed. All four registered campaigns passed
  again after integration, including the restored generic regression.

Artifacts used local `build/october-phase-published*` and
`build/october-phase-*-parent` directories. They are **not committed**, may not
exist elsewhere, and are not portable proof receipts. Rebuild and regenerate
evidence rather than copying an old executable and assuming validity.

No fresh aggregate line/branch/region collection accompanied this phase. An
older post-legacy-removal checkpoint recorded 4,617/4,617 covered source
functions, 190,968/202,741 lines, 104,401/155,940 native branch outcomes, and
173,573/183,450 regions. Those are historical source-coverage counts, not the
current 3,063-function selection census, and not current completion percentages.
The September `47` defect and `~9%` audit estimates were not newly verified
semantic totals. Do not extrapolate a completion percentage from them.

## 3. Bootstrap on the next computer

The commands below use a POSIX shell, suitable for Linux/macOS. For native
Windows, follow the platform setup documentation and translate environment
assignments; do not pretend POSIX commands work unchanged.

### Checkout and provenance

Start with a fresh clone, or inspect an existing checkout before changing it:

```sh
git clone --recurse-submodules https://github.com/davidly/dcc.git
cd dcc
git remote -v
git status --short --branch
git fetch origin
gh pr list --repo davidly/dcc --head test/ast-mir-proof-next --state all \
  --limit 5 --json number,state,headRefOid,mergeCommit,url
gh pr view 207 --repo davidly/dcc --json state,headRefOid,mergeCommit,url
git switch main
git merge --ff-only origin/main
git merge-base --is-ancestor 28f3c5a6 HEAD
git submodule update --init --recursive
git switch -c test/ast-mir-proof-phase2
```

The switch/fast-forward commands assume a clean checkout without conflicting
local work. If the ancestry check fails, inspect the handoff PR on GitHub:
finish any outstanding authorized publication first, or report the missing
checkpoint. Never reset a dirty checkout or discard unrelated work. Use a new
branch name if the example name already exists.

Record the full starting SHA, branch, host OS/architecture, tool versions, and
commands in a new machine-local evidence directory. Do not copy user-specific
paths, stashes, credentials, or worktree registrations from the outgoing host.

### Prerequisites and fresh tools

Follow [toolchain setup](docs/en/00-setup-toolchain.md) for the actual host:
Git, Python 3 (3.11+ recommended for the complete runner/report path),
PowerShell 7+, CMake, a C/C++17 build toolchain, `ntvcm`, and GitHub CLI for
publication. Use a matched Clang/LLVM coverage trio. LLVM 18 was used
historically; the invariant is matching Clang, `llvm-cov`, and
`llvm-profdata` major versions, not a copied machine-specific LLVM path.

Verify availability and build:

```sh
python3 --version
pwsh --version
cmake --version
clang --version
llvm-cov --version
llvm-profdata --version
command -v ntvcm
pwsh ./scripts/build-dcc.ps1
python3 scripts/runall.py --help
```

Restore missing dependencies using the documented platform setup after a
missing-tool failure; do not install arbitrary replacements or reuse an old
sanitized root binary. The canonical build must produce all host tools,
`dcc-debug-host`, and the platform-specific example I/O adapter.

If LLVM executables are versioned, export the installed matching paths through
`CC`, `LLVM_COV`, and `LLVM_PROFDATA`. Confirm their versions before collection.
The aggregate runner can instead resolve and set those variables itself:
use `-LlvmDirectory /path/to/llvm/bin`, or a local toolchain at `build/llvm/bin`.
On Ubuntu/Linux, automatic discovery also searches installed
`/usr/lib/llvm-<major>/bin` directories in descending major order after `PATH`
and the repository-local toolchain. Compiler-local companions precede `PATH`
companions; an unversioned compiler can find peers using its reported major.
For Ubuntu 24.04, the relevant packages are `clang-18`, `llvm-18`, and
`libclang-rt-18-dev` (in addition to the build prerequisites above). Use a
matching major available from the configured repositories on older releases.
Check it without starting proofs with
`pwsh ./scripts/run-mir-proof-suite.ps1 -PreflightOnly`.
Explicit environment overrides still win and must match; no machine-specific
session path is committed. If no local toolchain is present, install/restore
the matching tools rather than assuming they are bundled. This preflight
checks the LLVM trio only, not the sanitizer runtime or all proof prerequisites.
The current runner follow-up is Ubuntu-focused; native macOS and Windows
end-to-end validation remains outstanding.
Local runner evidence: all 147 Python tool tests passed, including five new
Ubuntu discovery/peer-selection tests. The discovered LLVM 18 trio also built
and ran both branches of a small ASan/UBSan-instrumented coverage control, then
merged and exported its profiles successfully. This is toolchain smoke
evidence, not a new eleven-phase proof collection or compiler-coverage gain.
PowerShell remains necessary for specialized proof harnesses even though
ordinary regressions now use Python.

On a reused checkout, do not assume `build-dcc.ps1` notices header changes.
Prefer a fresh worktree/build; if cleanup is necessary, inspect and remove only
the specific build outputs you own. Never recursively wipe a repository,
session directory, or another worker's worktree.

## 4. Freeze and revalidate the starting checkpoint

Choose a CPU budget appropriate to this host; examples use eight workers.
Create a unique artifact root and propagate nonzero command statuses:

```sh
mkdir -p build/phase2-baseline
git rev-parse HEAD > build/phase2-baseline/source-head.txt
export DCC_MIR_REQUIRE_COMPLETE=1
export DCC_MIR_REQUIRE_EMIT=1

python3 scripts/runall.py --mode full --extended --timeout 60 \
  --throttle-limit 8 --failures-only
python3 scripts/runall.py --mode full --extended --timeout 60 \
  --throttle-limit 8 --failures-only --no-stack-check

python3 scripts/mir-migration-census.py \
  --output build/phase2-baseline/no-stack.tsv --jobs 8 --timeout 60
python3 scripts/mir-migration-census.py \
  --output build/phase2-baseline/stack.tsv --extra-args=-fstack-check \
  --jobs 8 --timeout 60
python3 scripts/mir-extended-census.py --mode both --require-complete \
  --output build/phase2-baseline/extended.tsv --jobs 8 --timeout 60
python3 -m unittest discover -s scripts/tests -p 'test_*.py'
```

Capture complete logs without hiding the actual exit status behind `tail` or
`tee`. A POSIX shell pipeline does not automatically propagate the first
command's failure; use a direct redirection plus an explicit status check, or
Bash `pipefail`. Keep the strict MIR controls but remove unrelated ambient
mutation/forced-candidate/coverage controls from ordinary runs.

Rehydrate the four current campaigns through their registered scheduler:

```sh
python3 scripts/compiler-coverage-campaigns.py \
  --jobs 8 --campaign-jobs 2 \
  --build-dir build/phase2-baseline/audits \
  --raw-dir build/phase2-baseline/audits/raw \
  --campaign-dir build/phase2-baseline/audits/logs \
  --campaign status-pack-schedule \
  --campaign recursive-wide-product-schedule \
  --campaign recursive-frame-fill-schedule \
  --campaign byte-rotate-flags-schedule
```

These are CP/M runtime proofs, not host-native `int` comparisons. Diagnostic
compiler builds must remain isolated. Parent reproductions are available via
`--reproduce-parent` on status-pack, `--before-ref` on wide-product,
`--reference-compiler` on frame-fill, and `--reproduce-base` on rotate-flags.
Use `--help` and retained commit refs; rebuild reference tools locally.

Do not modify performance baselines to make a bootstrap failure pass.
Investigate any discrepancy with the outgoing counts; a newer main may
legitimately change inventory, but record the exact source delta first.

## 5. First implementation: bridge aggregate runners to Python

**Verified gap:** as of the handoff checkpoint, standalone `runall.py` is
available and used successfully, but aggregate orchestration still calls the
old runners:

- `scripts/run-mir-proof-suite.ps1`: two strict release commands invoke
  `scripts/runall.ps1`.
- `scripts/compiler-coverage.sh`: main stack/no-stack corpus commands invoke
  `runall.ps1`, and the extended command invokes `runall-extended.ps1`.

Do not describe those entry points as already Python-native, or run a long
collection assuming the gap has been fixed. This is the first bounded
implementation task on the next computer, **not part of this planning-only
handoff commit**.

Replace the corresponding regression execution on supported POSIX hosts with
the Python runner while preserving equivalent corpus discovery, extended C11
options, strict environment, stack/no-stack modes, workload inventory,
timeouts, performance checking, diagnostics, and profile propagation.
Explicitly prove that `DCC` selects the instrumented compiler and
`LLVM_PROFILE_FILE` reaches its children. Preserve required Windows behavior
where the Python runner is not yet supported; do not remove that platform.

Avoid duplicating main/extended execution merely because `--extended` combines
them. Map the old coverage workloads before changing them, retain all
specialized clobber/lifetime/fuzz/host/mutation gates, and compare exact
execution manifests. Test `scripts/tests/test_mir_proof_suite.py`,
`test_compiler_coverage_campaigns.py`, and directly related checkpoint tests.
Add focused invocation/environment/inventory tests for the bridge itself.
Runner command changes are part of checkpoint identity: create new receipts
and profiles, never reuse a pre-change success stamp.

Acceptance: matched expected/found workloads, explicit nonzero failures,
unchanged baselines, and a short instrumented smoke run with real nonempty
profiles. Commit this tooling increment separately from semantic compiler
changes. Do not turn the bridge into a broad runner rewrite.

## 6. Fresh coverage, then evidence-backed ranking

After the bridge passes and no workers edit source, collect one immutable
checkpoint. The aggregate runner has eleven phases and a completion receipt:

```sh
pwsh ./scripts/run-mir-proof-suite.ps1 -List
pwsh ./scripts/run-mir-proof-suite.ps1 \
  -Jobs 8 -MutationJobs 2 -MutationBuildJobs 2 -RunTimeout 60 \
  -OutputDirectory build/phase2-checkpoint-A
```

The second command is conditional on section 5 being complete. Do not use
`-RequireComplete` for the first gap-discovery run when gaps are still known;
it is the final exact-100% acceptance gate, not a way to suppress missing work.

For staged coverage, use a new absolute directory and the same toolchain
through all stages:

```sh
export DCC_COVERAGE_BUILD_DIR="$PWD/build/phase2-coverage-A"
export DCC_COVERAGE_JOBS=8
export DCC_COVERAGE_CLOBBER_JOBS=8
export DCC_COVERAGE_MUTATION_JOBS=8
export DCC_COVERAGE_CAMPAIGN_JOBS=2
export DCC_COVERAGE_CENSUS_JOBS=8
DCC_COVERAGE_STAGE=build sh scripts/compiler-coverage.sh
DCC_COVERAGE_STAGE=collect sh scripts/compiler-coverage.sh
DCC_COVERAGE_STAGE=report sh scripts/compiler-coverage.sh
```

Choose either the aggregate collection or staged coverage as the checkpoint
workload, not both back-to-back. A failed collection is not reusable success.
Record `inputs.json`, `build.json`, `collection.json`, their hashes, the source
SHA, execution manifests, logs, LLVM versions, and the aggregate receipt when
applicable. Profiles from faulty mutants must not enter accepted coverage.

Review `report/ast-mir-gaps.json`, `ast-mir-function-coverage.json`,
`ast-mir-function-detail.txt`, the branch review ledger, and current tests.
For each candidate gap, identify the actual semantic predicate, supported
input contract, reachability, equivalent prior proof, target consequence, and
cheapest falsifying test. Reject duplicate proof ideas before implementation.

Report all four metrics: functions, lines, native branch outcomes, and regions,
with unchanged source scope and explicit denominators. Keep executed coverage
separate from reviewed defensive/unreachable dispositions. Do not lower the
denominator, exclude inconvenient code, rename a timeout a mutant kill, or
treat fingerprints as mathematical semantic proofs.

## 7. Bounded four-worker proof wave

Aim for four useful implementation workers when independent work exists.
Assign them only after fresh ranking, with explicit GPT-6.1 Sol/medium,
isolated worktrees, bounded objectives, valid controls, and stop conditions.
The following are hypotheses/priorities, not claims of additional known bugs:

| Workstream | First hypothesis / deliverable | Ownership constraint |
| --- | --- | --- |
| Generic homes and forwarding | Prove that store-plus-cast forwarding retains homes for later branches, joins, calls, and PHI uses; add a compiler-source mutant removing that materialization and require a precise semantic kill. | One owner for generic emitter changes and the affected host/source-mutation harness. |
| Target-aware differential controls | Extend a genuinely missing boolean/byte/word alias-and-join contract with masked target oracles, deterministic seeds, minimized failing inputs, and ordinary/forced-generic comparison. | Own dedicated CP/M fixtures and data-only campaigns; coordinate before changing the shared generator. |
| Exact rejection and fallback | Choose the highest-risk unaudited active selector from fresh evidence. Prove named rejection, uncontaminated generic retry, and correct target execution of meaningful near-matches. | Different machine-family file from other compiler workers; one owner per shared C file. |
| AST/MIR metadata or CFG invariant | Choose an uncovered supported-input contract, prioritizing call signatures/K&R/default promotions, dominance/PHI edge uses, volatility, or invalidation boundaries without equivalent existing tests. | Only one worker may own `tests/host/mir_verify.c` and the shared mutation module. |

If proposed ownership overlaps, reduce concurrency or reorder tasks; do not
send four workers into one shared compiler file. Never keep workers busy by
inventing unrelated work. Shared campaign registration and handoff updates
belong to the integrator. Individual workers do not push to the shared branch.

Each worker returns: exact commit, parent defect reproduction or test-only
gap classification, oracle definition, selected MIR/assembly evidence, valid
controls, mutant results, focused sanitizer/debug needs, and limitations.
The integrator reviews and runs the integrated cases independently; it does
not accept a self-report or an additive-only diff as proof of safety.

## 8. Proof and target requirements

- Target ABI: signed plain char; 16-bit int/short/pointers/size_t; 32-bit long
  and float; no target double or long long. Use explicitly masked 8/16/32-bit
  arithmetic and defined shifts. Avoid signed overflow, out-of-bounds access,
  unspecified evaluation order, and other undefined behavior as oracles.
- Preserve source spellings, renamed variants, K&R/default-promotion forms,
  and proven signed/unsigned variants. An overly narrow guard can be a
  performance regression even when generic output is correct.
- Prove the intended function actually contains the PHI, spill, call, alias,
  or backedge under test. Source syntax or a selector label alone is not proof.
- Machine-only diagnostic mutations are restored before generic fallback.
  Comparing that fallback against the original program does **not** prove
  same-mutated-MIR semantics. Use a private persistent diagnostic compiler,
  injected at a justified point before verification/selection, or an
  independently justified equivalent source oracle; state which one.
- Classify inactive/equivalent fields instead of demanding all mutations
  reject. Stable assembly hashes alone do not justify a semantic equivalence
  classification. Explain opcode inactivity or the proven equivalent location.
- Genuine compiler mutants must build, run a passing unmutated control, and
  fail a named semantic assertion. Crashes, build failures, and timeouts are
  invalid results, not kills. A deliberate stack/range witness must be labeled
  separately from a defined-behavior target oracle.
- Reject unsupported/cyclic/volatile/aliased shapes conservatively and
  transactionally. No production app/function-name gates, legacy oracles,
  hidden semantic fallback, or baseline-driven selector recognition.
- CP/M artifacts and staged fixtures require unique 8.3 basenames; source
  filenames may be longer. Respect `tests/_test_overrides.json` and timeouts.
- Do not discard a failing defined-C near-match just because it selects a
  generic emitter. Restore the control, establish parent/current behavior,
  and fix a tightly coupled defect before declaring that proof complete.

## 9. Integration and release gates

Freeze an integrated tree before expensive gates. Keep one combined CPU
budget across builders, targets, and mutant children; four agents do not each
own the entire machine. Eight clobber workers is a historical measured
default, capped by actual available capacity, not a universal speed claim.

For compiler changes, compare both before/after main censuses with
`--compare` and `--fail-on-regression`, inspect every changed app/function,
and run all changed apps in Python full stack/no-stack mode. At the phase
integration boundary run both strict full+extended Python commands from
section 4 with checked peep/nopeep performance. Never rewrite baselines to
accept a regression.

Run independent normal and sanitized host tests with isolated executable
output directories:

```sh
cmake -S src/dcc -B build/phase2-host -DCMAKE_BUILD_TYPE=Release \
  -DDCC_BUILD_MIR_TESTS=ON \
  -DDCC_RUNTIME_OUTPUT_DIRECTORY="$PWD/build/phase2-host/bin"
cmake --build build/phase2-host --parallel 8
ctest --test-dir build/phase2-host --output-on-failure

cmake -S src/dcc -B build/phase2-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DDCC_BUILD_MIR_TESTS=ON \
  -DDCC_RUNTIME_OUTPUT_DIRECTORY="$PWD/build/phase2-sanitize/bin" \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build/phase2-sanitize --parallel 8
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/phase2-sanitize --output-on-failure
```

Also compile/exercise the changed recursive/CFG/ownership proof cases under
ASan/UBSan, not only unchanged host tests. Do not overwrite root `dcc` with
the sanitizer build. Rebuild canonical tools if that isolation is breached.

For source-debug contracts:

```sh
cmake -S src/dcc_debug_host -B build/phase2-debug-host \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/phase2-debug-host --parallel 8
ctest --test-dir build/phase2-debug-host --output-on-failure
```

Review frame/register/constant locations, source boundaries, addresses,
scopes, and assembly rewriting for every compiler/peephole edit. Preserve full
`-g` metadata and release-identical `-gline` output through peep and nopeep.
`dcc-debug-host` is the source debugger; `ntvcm` remains the ordinary emulator.

Extend and run the compiler-source mutation campaign as appropriate:

```sh
pwsh ./scripts/run-mir-compiler-mutations.ps1 \
  -Jobs 2 -BuildJobs 2 -OutputDirectory build/phase2-source-mutants
python3 -m unittest discover -s scripts/tests -p 'test_*.py'
git diff --check
```

Use the aggregate suite periodically at stable milestones, not after every
small test. Test-only increments may reuse unchanged release evidence only
when exact source/tool dependencies match; every added case must still run.
Runtime edits additionally require `rtl-iy-safety.py` and
`audit-runtime-coverage.py`, applicable ABI checks, and linked target tests.

## 10. Publication, completion, and remaining work

Before publication, record fixes versus test-only proof gaps, valid controls,
source/target oracle limitations, exact execution inventory deltas, all four
coverage metrics, changed output/performance, and precise failed experiments.
Update this document's current checkpoint, the historical handoff, and
`docs/compiler-coverage.md`. Keep generated artifacts under `build/`, not Git.

Commit small verified increments, including:

```text
Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>
```

Push locally verified commits without waiting for Actions; verify the remote
head matches. Preserve unrelated changes and never amend or force-push
without authorization. Do not auto-merge the next implementation PR merely
because this handoff PR was authorized for merge.

The next bounded phase is complete only when the runner bridge, fresh
checkpoint/ranking, selected proof increments, exact-tree local gates,
evidence documentation, and commit/push are done. If a baseline or new control
fails, investigate rather than publishing a success-shaped receipt.

An eventual 100% claim needs two clean matching exact-tree collections with
the four source-coverage metrics genuinely complete, valid workload/provenance
receipts, and all correctness/performance/debug/sanitizer gates. Document
defensive/unreachable outcomes separately without quietly changing the scope.
Even 100% executed coverage is not a mathematical proof for every C program.

## 11. Copy/paste resumption request

> Read `docs/ast-mir-next-phase-handoff.md` completely, then load its linked
> dcc-project and mir-migration skills. This is the authoritative cross-machine
> handoff; assume no previous session history or transferable build artifacts.
> Use GPT-6.1 Sol (`gpt-6.1-sol`) at medium reasoning effort for yourself and all
> implementation workers. Inspect checkout/remotes/GitHub merge state; start
> from current main containing `28f3c5a6`, preserve unrelated work, and rebuild
> all machine-local tools/evidence. Use Python `scripts/runall.py` for ordinary
> compiler application/unit regression testing, not `runall.ps1`; run Python
> proof-tool unit tests with `python3 -m unittest` as well. First revalidate and
> freeze the baseline, then bridge aggregate
> regression calls to Python with workload/environment tests before fresh
> coverage. Rank meaningful gaps and complete one bounded, up-to-four-worker
> proof wave with isolated worktrees, target-aware oracles, selector rejection
> and real generic fallback, compiler mutants, and required local release,
> performance, sanitizer, and debugger gates. Fix reproduced failures rather
> than deleting controls. Keep honest denominators and distinguish inactive
> mutations, source-reachable defects, and diagnostic MIR witnesses. Update the
> handoff/coverage ledger, commit and push verified increments without waiting
> for Actions, then stop at the phase boundary. Do not auto-merge a future PR
> or claim the broader 100% goal from passing current tests. Begin with verified
> state and the first concrete action, then implement.
