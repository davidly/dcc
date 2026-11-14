# Fix plan for the logged compiler bugs

[compiler-bugs.md](compiler-bugs.md) has twenty-four open bugs found by the
clang-oracle fuzzer (`.github/skills/clang-oracle-testing/SKILL.md`,
`docs/docs/en/appendix/06-differential-testing.md`). Many are one defect seen
from several angles. This plan groups them by *probable root cause* so that
one fix, made in the right place, closes several issues, and so that the
fuzzer can confirm it did.

The clusters are hypotheses drawn from the shapes that fail and the
neighbouring shapes that pass in each entry. A1 (#281), A2a (#283), B1 (#286),
K (#278), L (#279), F (#296) and G1 (#295) are confirmed and fixed.
B3 confirms the common
forwarded-operand defect in BUG-026 and the formerly E-grouped BUG-017. The A2 hypothesis was disproved by its first
root-cause experiment and has been split into A2a and A2b below; the
B was likewise disproved and split into B1–B4. I was disproved and split
with maintainer approval into I1 (return admission) and I2 (cast signedness);
both are now fixed. Other remaining clusters are still hypotheses.
If the first fix in another cluster does not clear its siblings, stop and
report; do not force unrelated defects into one change. The maintainer
explicitly approved K as a four-rule exception, and J2 as two independent
fixes with narrowly specified existing-baseline exceptions. J2's numeric
stdout refreshes still require independent host-clang proof.

## Rules

- One cluster per branch and pull request. The A subclusters and B–D all touch MIR
  lowering and the homed/spilled emitters and will conflict; work them one
  at a time. I1, I2, K and L are isolated and can proceed in parallel.
- Do not change a generator's `KNOWN_BUGS` or a reproducer until the fix is
  in. The parked reproducers and the steering are the acceptance test.
- Acceptance for every bug in a cluster, from the skill's "Closing a logged
  bug" section:
  1. `python3 scripts/run-known-bugs.py` reports the cluster's reproducers
     `FIXED` (peep and nopeep) and every other reproducer still `fails`.
  2. The cluster's features are removed from `KNOWN_BUGS` and a 300-seed
     batch per affected generator with `--off known` is clean.
  3. `python3 ./scripts/runall.py -mode=full -extended` is `SUCCESS`, and
     **no existing row of `tests/perf_baselines.csv` changes**. Every
     program in `tests/` compiles correctly today, so a fix for code that
     is wrong must not alter the code emitted for code that is right. A
     changed baseline, in either direction, is a sign that the fix is wider
     than the defect; understand it before going on, and never move a
     baseline to absorb a regression.
  4. Reproducers are promoted to `tests/` (`tests/known-bugs/README.md`)
     as **new** unit tests with **new** `perf_baselines.csv` rows measured
     on the normal stack-check `runall.ps1` path, entries closed in
     `compiler-bugs.md`, issues closed from the commit.
- The goal is still concise, fast Z80. A fix is the narrowest change that
  makes the wrong shape right, not a general fallback to a slower or larger
  schedule. Compare peep and nopeep output for the reproducer before and
  after, and prefer a fix that emits for the bad shape what the compiler
  already emits for the neighbouring good shape (each entry lists them). If
  the only correct fix costs code in the new tests, note the cost in the
  pull request; it must never cost code in the existing ones.
- Run the regular MIR gates for compiler changes
  (`DCC_MIR_REQUIRE_COMPLETE=1 DCC_MIR_REQUIRE_EMIT=1` strict runall, debug
  metadata tests when statement or scope boundaries move; see
  `.github/skills/dcc-project/SKILL.md`).
- A fix that flips a reproducer outside its cluster is information: record
  which, and move that bug into the cluster in this file.
- Cluster pull requests are **not** gated on `pwsh ./scripts/prepush.ps1`.
  That exhaustive proof suite (compiler mutants, sanitizers, instrumented
  coverage) takes five to six hours and is the maintainer's periodic manual
  check, run on `main` after a batch of clusters has merged. The gate for a
  cluster PR is the acceptance list above plus the regular script unit
  tests; do not run or wait for prepush. Two of those unit tests
  (`test_mir_campaign_baselines`, `test_mir_exact_controls`) fail on `main`
  today for reasons unrelated to any cluster (#277); report them as
  pre-existing, do not refresh their expectations in a cluster PR.

## Clusters

Priority follows the maintainer's own ranking of what ordinary code hits
(BUG-026, 044, 047, 030, 003, 013, 016, 025) and the number of issues a
fix would close.

### A1. Conversions discarded while block-local load types are unresolved

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-025 | #245 | `return x;` of a block object not converted to 8- or 32-bit return type |
| BUG-031 | #251 | Block 32-bit local copied from a local, stored through a variable address: low byte only |
| BUG-007 | #227 | Block or `for`-init local as a converted argument of an indirect call: bad ABI type |

**Confirmed root cause**, fixed on `fix/cluster-a1-deferred-conversions`
from main `48f4f3de`: an unresolved block-local load loses its required
conversion, and argument replay can replace a fixed prototype's ABI type
with a repaired operand type. Declaration replay restores the load's
declared type but not the consumer's conversion contract.

Return and indirect-store requirements now survive on the consumer until
types resolve, without speculative MIR instructions or value IDs. Fixed
prototype arguments retain their ABI target; existing exact call schedules
and selection-time conversion materialization are preserved.

- Before promotion: 3 `FIXED`, 43 `fails`, with no outside-cluster flips.
  BUG-004, BUG-008 and BUG-015 remain in A2.
- Both censuses compare identically across 3,333 existing functions. Strict
  full+extended stack-check and no-stack-check runs pass; no existing
  performance row changed. The earlier speculative-conversion experiment
  changed 355 functions across 80 apps and was rejected.
- For BUG-025, `ld hl,300` becomes `ld hl,44`, exactly matching the
  unchanged function-scope control in peep and nopeep.
- Restored `ptrcallconv`, `blockret` and `blockwidecopy`: seeds 1-300 in
  all production configurations finish with 218 ok, 81 INVALID, one
  target-limit-only, zero FAIL. Seeds 77/256 exposed arithmetic-argument
  facets of the same ABI defect; both have permanent regression coverage.
- Promoted as `tblkabi`, `tblkret` and `tblkcopy` with unchanged host
  baselines and three new PowerShell stack-check performance rows.
  Focused MIR, ASan/UBSan, debugger/metadata, release-identical `-gline`
  and require-emit checks pass.
- The final regular Python gate has three failures and two skips across
  307 tests. The two #277 failures match clean rebuilt main exactly.
  The additional mutation-runner exit-139 fixture failure also reproduces
  on clean main, with nondeterministic mutant IDs (#280). The maintainer
  approved documenting it separately without fixing or skipping the test.

### A2. Disproved: split into A2a and A2b

The A2 group (BUG-004, BUG-008, BUG-015) was attempted on
`fix/cluster-a2-block-typing` from main `48f4f3de`. Its first confirmed
root-cause fix cleared **only BUG-008**; BUG-004 and BUG-015 still failed and
no other reproducer flipped. Per the stop rule the agent stopped and the
maintainer approved the split below. The worktree experiment was not
committed; A2a re-derives it cleanly.

### A2a. Constant payload mistaken for an object displacement

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-008 | #228 | Negative/high-bit constant assigned to an initialized block 32-bit local: fatal |

**Confirmed root cause:** `mir_spilled_structure_valid` in
`src/dcc/dcc_mir_spilled_cfg.c` treats every object-associated instruction's
`immediate` as an address displacement. Declaration repair can fold a named
block assignment into `MIR_CONST` while retaining its object metadata, so for
`b = -3` the valid unsigned 32-bit payload `4294967293` is checked as an
offset and the function is rejected. The function-scope control has the same
payload but no object index and passes; positive constants and an
uninitialized block-local control also pass.

Fixed on `fix/cluster-a2a-constant-payload` from main `11d4454b`: exclude
`MIR_CONST` alone from that displacement check. No lowering or emission
schedule changed; this is a preflight-validation defect, not an expression
typing defect.

- Before promotion: one `FIXED`, 42 `fails`, with no outside-cluster flips.
  BUG-004 and BUG-015 still fail and remain in A2b.
- The fixed signed and unsigned functions exactly match their existing
  function-scope controls' peep and nopeep assembly. Positive, uninitialized
  and 16-bit controls are byte-identical before and after; explicit casts
  and high-bit constants also pass.
- Both censuses are byte-identical across 3,363 existing functions. Strict
  full+extended stack-check and no-stack-check runs pass; all 499 existing
  performance rows are unchanged.
- Restored `blockwideneg`: integer seeds 1-300 in all production
  configurations finish with 215 ok, 84 INVALID, one target-limit-only,
  zero FAIL.
- Promoted as `tblkneg` with the unchanged host baseline and one new measured
  PowerShell stack-check performance row. CMake MIR verifier, debugger,
  require-emit, full-debug and release-identical `-gline` checks pass.
- The final regular Python gate has two failures and two skips across
  307 tests. Both failures match clean rebuilt main `11d4454b` exactly
  (#277); no expectations were changed. An initial overlapping run's
  mutation-fixture timeout and cleanup error do not recur in the final
  standalone full run.

### A2b. Remaining block-local expression typing (E handoff)

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-015 | #235 | Block pointer to 32-bit object: `*q *= k` in 16 bits, `c <= *q` signed; block `unsigned char` compared as `unsigned int`; `?:` of two block floats |
| BUG-004 | #225 | `&&`/`||` assigned to a block 32-bit local leaves garbage high word (facet; the function-scope half is in E) |

Neither is fixed by A1 or A2a. Their shared nested-block condition is not
proof of a shared root cause, and BUG-015 itself has many facets. Work A2b
**after E**: E1 fixes only BUG-004's function-scope half; the block-local
assignment remains. Its boolean source stays untyped after the canonical
conversion is repaired to long (E2 below), independently of BUG-015's
pointer/byte/cast facets. E1 also corrects BUG-015's wrapped word decrement
widening, but none of those remaining facets. Trace the remaining issues
independently rather than assuming their shared block scope is a root cause.
Trace expression typing, conversion insertion and deferred metadata repair in
`src/dcc/dcc_mir.c` against the neighbouring correct shapes before changing
anything. The stop rule applies: if a first fix clears one facet of BUG-015
but not the others, report rather than stack independent fixes.

### B. Disproved: split into B1–B4

The original B group (BUG-014, 019, 026, 030, 036) was attempted on
`fix/cluster-b-stack-homes` from main `38952e7a`. Repairing BUG-030 cleared
only that reproducer; the other four failed identically and no outside-cluster
reproducer flipped. The grouping was wrong, and the experiment itself was
rejected because it changed six existing functions. The maintainer approved
the split below.

### B1. Stale probe offset for a `for`-init declaration

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-030 | #250 | C99 `for`-init declaration overlaps enclosing block's locals after an earlier sibling scope |

**Confirmed root cause:** AST support probes replay a `for`-init declaration
before earlier sibling scopes have reserved their storage. In
`array_in_else`, the probe creates `i#0#0` at offset -7; real declaration
replay later records its final offset as -11. `mir_get_object` retains the
first object's offset and `mir_scalar_memory_location` prefers it over the
corrected declaration table, so the counter overwrites the array at `ix-7`.
LLDB confirmed both offsets and their probe/replay origins.

The first experiment refreshed the object's offset from
`mir_note_declared_symbol` during real declaration replay. It fixed BUG-030
in both modes with controls byte-identical, but **changed six of 3,366
existing functions** across `catalan`, `tcmpabi`, `tforblk`, `tforsco` and
`tstr`; four lost their exact schedule and fell back to
`spilled-scalar-cfg`. That is not acceptable. It also shows that those
existing functions carry probe-stale offsets today — harmless ones that do
not overlap — and their layouts are what the exact schedules match.

Constraint for the real fix: existing functions must keep their current
frame layout byte-for-byte. Candidate placements, in order of preference:

1. Stop the probe from creating or retaining the object (or make probe and
   replay reserve storage in the same order), so no post-hoc offset repair is
   needed and existing layouts are untouched.
2. If a repair after replay is unavoidable, apply it only when the stale
   offset actually overlaps another live object in the same frame; emit the
   current layout otherwise.

**B1 implementation:** use the overlap-qualified repair at real `for`-init
replay, not a blanket refresh. Probe replay retains non-call initializer MIR
(`mir_neutralize_since` only discards call-bearing captures), so discarding
probe objects globally would also change the established homes and schedules.
The repair checks half-open byte ranges against still-live enclosing locals;
for-init entries whose rename has already been popped are not live, despite
remaining in `locals[]`. Ordinary declarations are outside this repair.

An initial all-local overlap check also moved `tcmpabi.insertion_sort`'s
ordinary `j` past an expired LICM temporary and mistook expired sibling-loop
slots in `tforsco.main` for live objects. That iteration was rejected. The
for-init/active-rename guards preserve both functions without weakening the
actual overlap check.

Both final censuses compare identically across all 3,366 existing functions
(stack-check and no-stack-check): zero selector, size, instruction or selected
assembly-hash changes. BUG-030 alone flips to `FIXED`; the other 41 parked
reproducers still fail. The two neighbouring controls are byte-identical in
peep and nopeep. Strict full+extended stack and no-stack suites pass.

**Accepted:** `armfordecl` is restored; integer production seeds 1-300 finish
with 216 ok, 83 INVALID, one target-limit-only and zero FAIL. BUG-030 is
promoted as `tforhome`, with additional 16-/32-bit counters and a new normal
stack-check performance row; all 500 existing rows are byte-identical. The
final censuses add only its 11 new functions, with all 3,366 existing functions
unchanged in both modes. Both final strict full+extended gates pass (516 apps,
196 extended apps); all 41 remaining known-bug reproducers still fail.

The full Python gate runs 307 tests: 303 pass, two pre-existing skips, and the
two #277 failures have byte-identical failure payloads on an unchanged
`0d11922d` snapshot using the saved parent compiler. The nondeterministic
#280 mutation fixture passes here and is untouched. Its separate unchanged-main
confirmation run also encountered existing mutation-runner timeout/cleanup
errors; none occurred in the full fixing-branch run. Debug metadata/host tests
and the six ASan/UBSan MIR tests pass; `-gline` retains release-identical
binaries and full `-g` runs pass in both modes.

Both censuses must continue to require zero existing-function changes. The rejected patch, LLDB trace, assembly and
census comparisons from the first attempt are in the agent's
`build/cluster-b/` (not committed); re-derive rather than reuse.

### B2. Disproved: split into B2a and B2b

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-014 | #234 | 32-bit local with two definitions, incremented, stored to a global: `missing direct MIR named-home offset` |
| BUG-019 | #239 | Unreachable declaration after `return`/`break`/`goto` in a nested scope: `MIR emission is required` |

Both are compile-time fatals where a declared local has no home in the view
the emitter uses. Not the B1 offset defect (unchanged by it). Trace where the
named home is looked up for BUG-014 and where an unreachable scope's
declaration is dropped for BUG-019 before assuming they share a cause; stop
and split if the first fix clears only one.

The first root-cause fix clears BUG-014 only; BUG-019 still fails with
`MIR emission is required`. The shared-home hypothesis is disproved.
Under the approved stop rule, B2a is completed on this branch and B2b is
left untouched for separate work.

### B2a. Redundant narrow preload of a fused wide increment

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-014 | #234 | A two-definition 32-bit local incremented and stored to a global |

The spilled emitter fuses the increment into its authoritative named home.
`mir_value_has_direct_named_home()` recognizes that binary result, and the
wide loader resolves its offset via `mir_binary_is_selfstore_wide_increment()`.
However, the generic store first invokes the narrow loader before loading
all four bytes. That unnecessary preload attempts to resolve the binary
instruction as a plain load/object and fatals. Suppress that preload for
the already-proven fused increment; retain the existing wide load and fused
schedule, without changing homes, frames or candidate selection.

Acceptance: before promotion, only BUG-014 flips (one `FIXED`, 35 `fails`);
after promotion all 35 remaining reproducers still fail. `twincg` adds the
logged spellings plus carry, unsigned wraparound, loop, untaken-arm and
neighbouring decrement/add-two controls. Their baseline is host/sanitizer
proven and its only new perf row is measured by normal-stack PowerShell.
Integer production seeds 1-300: **213 ok, 85 INVALID, two target-limit-only,
zero FAIL** after restoring `widelocalinc`; a 3,000-seed steering proof is clean.

All **3,426 existing functions are byte-identical in both MIR censuses**,
and all **507 existing perf rows are unchanged**. Strict full+extended stack
and no-stack suites pass; the promoted app passes full mode in both. CMake
MIR (6/6), debugger host (10/10), line-debug (2/2), compiler-entrypoint (13/13)
and require-emit tests pass. The fixed shape retains release-identical
`-gline` output and full-debug local/frame metadata through both pipelines.
The final Python gate runs **312 tests: two approved #277 failures, two
existing skips**. #280's mutation fixture passes. No expectation, skip or
baseline was refreshed; no dating checks or `prepush.ps1` run were made.

### B2b. Unreplayed declaration in an unreachable nested scope

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-019 | #239 | Unreachable nested block or for-init declaration after an exiting statement |

Traced independently, not fixed here: `mir_lower_stmt()` lowers every child
of `AST_COMPOUND`, including a dead declaration placeholder. The metadata
walk in `ast_process_stmt_metadata()` skips a dead nested compound/for, so
`mir_begin_declaration()` / `mir_end_declaration()` never replay that
placeholder. The resulting MIR retains `decl`, a load of unresolved `b`,
and a scope restore after the return; no generated candidate accepts it.
Function-scope declarations take a different replay path. This is a
lowering/metadata reachability mismatch, not BUG-014's wide-store reload.
Preserve labels/re-entry, declaration diagnostics, scope/VLA accounting and
debug metadata when investigating separately. No B2b code was changed.

### B3. Same-value binary operands read an unallocated slot

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-026 | #246 | `d = a - b; return d * d;` reads a stack slot the function never allocates |
| BUG-017 | #237 | `x &= x` on a parameter, returned through a widening cast, gives 0 |

Confirmed shared root: the first operand consumes a just-forwarded value,
then the second use reloads its unwritten or unallocated spill home. The
spilled emitter now snapshots the repeated-forward identity and duplicates
HL into DE, or retains DE:HL after saving the first wide copy. The unrelated
BUG-017 flip is the same `binary v0,v0` defect before widening, not a typing
fix; it is moved here from E and promoted too.

Regressions: `tselfop` (word/long and nested/mixed float facets) and
`tandself` (parameter self-AND, casts and controls). Original neighbouring
controls are unchanged in both modes, as are 3,394 existing census functions
in both stack modes and all 504 existing checked performance rows. Only the
two new tests add measured normal-stack-check rows. Strict full+extended
stack/no-stack, host/sanitizer and debug checks pass.

Final production seeds 1-300, `--off known`: integers **213 ok, 85 INVALID,
two target-limit-only, zero FAIL**; floats **267 ok, 33 INVALID, zero FAIL**.
The integer seed-255 peep-only failure reproduces with unchanged current
main and is independently parked as BUG-054 / #288 (K2 below), not fixed or
masked by B3. Its byte-only `peepbyteself` steer leaves word/long/float
self-operands restored; 3,000 generated seeds contain no disabled shapes.
After promotion all 37 remaining parked reproducers still fail; the final
censuses add only the 29 new test functions. Both strict full+extended gates
pass (545 applications), including the focused stack-check performance gate.
The Python suite runs 312 tests: two #277 failures have identical payloads
with unchanged current main; three unmodified mutation-harness errors remain
(two 180-second timeouts and a missing watchdog-child file), with two existing
skips. The timeouts also occurred in the unchanged-main fixture check. A
separate cleanup-case check fails on both clean current-main scripts and the
branch, though its nondeterministic symptom differs (8-second time budget
versus the missing child file). The full Python gate is therefore not green;
no expectation, skip or harness fix is included. The 25 focused integer,
generator and line-debug tests pass, as do all ten debugger-host tests.

### B4. Loop-local store dropped when the value is also a call argument

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-036 | #256 | 32-bit local assigned in a loop and passed directly to a call: store dropped, reloaded from a slot never written |

Confirmed independent of C/D: `mir_call_argument_cache_target_for_state()`
scanned uses only after the definition, missing the earlier loop-header PHI
whose backedge consumes that value after the call. Slot planning/emission
therefore treated a loop-carried value as call-only, staged it on the argument
stack or in alternate registers, and never initialised the spill home that the
PHI edge later reloads. The named store is legitimately dropped for a promoted
object; the lost SSA spill is the defect, not object dead-store elimination.

Before changing the compiler, compared MIR/assembly for C's 023/027/029/037/
038/052 and D's 021/024 (the width/value-rejection facets do not use this
cache). Their failing kernels have no corresponding direct call-argument
handoff. After the narrow PHI-use guard, all six C reproducers and all six D
reproducers still fail identically; no out-of-cluster reproducer flips. This
shares the general uninitialised-forward-home symptom of B3, but the fault is
in call-cache planning, not repeated binary operands or fused operations.

Initial proof: BUG-036 passes in both modes, all other 33 parked reproducers
still fail, and all 3,436 existing functions are byte-identical in both strict
MIR censuses. The expression/narrow-argument neighbours are unchanged. The
same emitter now writes the needed four-byte spill before the call; no
selector, schedule, profitability gate or fallback changes.

Regression `tlpcall` adds the logged long/float facets and controls. Production
seeds 1-300 after restoring `loopcallarg` / `floopcallarg`: integers **228 ok,
72 INVALID, zero FAIL**; floats **288 ok, 12 INVALID, zero FAIL**. Both
3,000-seed disabled-shape proofs pass. Only the measured new performance row
is added; all 509 existing rows are unchanged. Strict full+extended stack and
no-stack gates and the Python full+extended runner pass, as do new-app host
validation and full mode in both stack modes. CMake MIR checks (6/6), host
ASan/UBSan, require-emit and 38 focused compiler/generator/line-debug/
entrypoint tests pass. Release/line-debug binaries match in both pipelines;
full-debug runs retain local/frame metadata and correct output.

The final censuses add only `tlpcall`'s 25 functions: all 3,436 existing
rows remain byte-identical in both modes. Both final strict full+extended
gates pass (549 apps: 525 pass, 24 existing skips), as does the promoted app's
normal-stack performance validation. Its only new row is peep 556,423 cycles /
8,960 bytes and nopeep 560,949 cycles / 9,344 bytes.

The full Python gate runs 312 tests: the two approved #277 failures, two
further timing-bound harness failures and five mutation scheduler errors
(three timeouts, an associated `CMakeScratch` teardown race and a missing
watchdog-child file), with two unchanged skips. These additional fixtures use
synthetic compilers and byte-identical current-main scheduler scripts, not
the dcc emitter. An isolated unchanged-current-main source fixture run passes
four of the five affected cases and reproduces the serial/parallel fixture's
180-second timeout; no compiler dating/build was involved. After the long
jobs, the current branch's clobber harness and survivor/invalid-exit fixture
pass (three tests), including #280's exit-139 control. The three remaining current-branch timeout/
watchdog/nested-scope cleanup case retries pass as well. Thus only the
serial/parallel timeout remains reproduced in the unchanged-main fixture;
no full-suite green result is claimed.
The CMake Debug debugger suite passes 9/10: its synthetic MI Ctrl-C fixture
fails terminal-endpoint cleanup twice. The unchanged canonical optimized
`dcc-debug-host` passes that same MI test; all compiler-generated debug
fixtures pass. No harness, expectation, skip or old baseline was changed;
no dating checks or prepush.ps1 were run.

### C. Assignments lost across control-flow merges and loop-carried values

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-027 | #247 | Local assigned in `if … else` nested in an `if`, then updated: lost when a loop follows |
| BUG-038 | #258 | Same with no outer `if`: lost when an early-return `if` follows |
| BUG-029 | #249 | 8-bit local and another variable assigned in opposite arms: `(int)c + x` loses `x` |
| BUG-037 | #257 | Copy of a 32-bit loop counter is taken after the increment |
| BUG-052 | #272 | Folded `?:` on a 32-bit parameter in a loop accumulates into the parameter |

BUG-023 was filed here but is confirmed fixed in D2a: its postfix update
uses the same false named-home binding as BUG-021.

The first three are a value merged from two arms that is never stored before
the update. BUG-037 and BUG-052 are a value read at the wrong point of the
loop. Both smell of the homed emitter's liveness/merge at join blocks
and of a temporary being homed in the variable it was computed from. Begin
with BUG-038, the smallest.

### D. Post-increment and post-decrement

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-003 | #224 | `++`/`--` on a 32-bit array element done in 16 bits |
| BUG-044 | #264 | Float `++`/`--`: no-op on a scalar, 16-bit integer increment on an array element |
| BUG-021 | #241 | `x++` yields the new value (16-bit local from a local array element) |
| BUG-023 | #243 | Cross-type local assignment followed by a postfix update/branch; moved from C |
| BUG-024 | #244 | `if (x++)` in a loop tests the new value (8-bit local) |
| BUG-010 | #230 | Cast or narrowing return of a 32-bit post-increment: rejected |
| BUG-041 | #261 | Value of a float `++`/`--` rejected |

**Confirmed split: D1 (003/044) and D2 (021/023/024/010/041).** The first
root-cause change fixes D1 only; do not extend it with independent D2 fixes.
BUG-044 is the maintainer's second priority.

- **D1 — typed update arithmetic:** `mir_lower_incdec` leaves array operands
  untyped when their result is discarded. Memory repair widens the load/store
  but not the arithmetic or its unit constant, so a long/float index still
  updates a word. Even a known float scalar gets the integer bit pattern 1
  instead of float 1.0. Normalize the operation and private unit constant to
  the stored type, including after deferred pointer/block-lvalue repair.
  Existing byte/word/pointer schedules are untouched; new wide updates use
  the same carry/borrow or float helper sequence as `+= 1` / `+= 1.0f`.
- **D2 / BUG-021:** MIR returns the old value (`v15`) correctly, but the
  spilled schedule updates `l3` in place and then reloads that overwritten
  named slot for the return. This is a stored-home/value-lifetime defect,
  not an arithmetic-width defect; the narrowing conversion gives the old
  value a named local home.
- **D2 / BUG-024:** MIR branches on the old header PHI (`v5`) correctly.
  `mir_phi_uses_scalar_object_home` admits a byte PHI unconditionally; the
  spilled schedule stores the new value into the same byte local and then
  reloads it to test the old PHI. The later do-while prevents full promotion.
- **D2 / BUG-010:** the AST admission gate for postfix values recognizes
  plain integers/pointers, not long values. `ast_value_is_long_word` has a
  special long-postfix path, explaining why direct assignment works, but
  casts/narrowing returns first require `ast_expr_supported` and reject it.
- **D2 / BUG-041:** AST increment/decrement and float-value classifiers do
  not admit float update values. Repairing statement arithmetic cannot make
  those rejected expressions reach MIR. This needs separate admission work.

D1 acceptance is recorded below and in Tracking. D2 is split into **D2a
(021/023/024: stored-value lifetime)** and **D2b (010/041: AST admission)**;
D2b is being investigated independently.

#### D2a root-cause experiment

Between BUG-021 and BUG-024, the apparent shared home-clobber cause is only
a semantic resemblance, not one faulty gate. BUG-021's `v15` is an assignment conversion (`MIR_UNARY`)
carrying the destination name `l3`; that name is metadata, not a binding of
its immutable SSA value to the object's mutable memory. The selfstore
increment matcher nevertheless accepts it as a named memory source. In the
promoted-local-slot candidate, the old SSA value occupies `l3`'s vacated
slot; the in-place update then overwrites it. Require an actual LOAD,
PARAM or PHI binding before using the selfstore update. The existing
spilled candidate computes the update from the SSA value instead; no
fallback or selector/cost-policy change is added.

The single root fix clears **021/023**, while **024 still fails** in all
four parked variants. Per the approved partial-fix stop rule, no second
compiler change is added. BUG-024 is independently admitted by the
unconditional byte case of `mir_phi_uses_scalar_object_home`: `v5` really
is a header PHI, and the later store of `v14` destroys its old value before
the condition reloads it. It needs its own lifetime proof, not the
assignment-conversion binding fix.

**Confirmed same cause: BUG-023, moved from C.** Its cross-type assignment
also gives a `MIR_UNARY` result destination-name metadata. The later
selfstore update treats that metadata as an authoritative memory binding,
losing the old value used by the branch or the assigned loop-carried value.
The existing single matcher fix clears all eight output cases in both
modes; this is not an incidental flip. It is promoted as `tpostbr` alongside
`tpost16`. No additional compiler fix is needed or added.

#### D2a initial acceptance (BUG-021 promotion)

- BUG-021 is promoted as `tpost16`, with the original host-proven baseline
  and one newly measured normal-stack performance row. Before promotion:
  **28 reproducers: 2 FIXED (021/023), 26 fails**. After the initial 021
  promotion: **27 reproducers: 1 FIXED (023), 26 fails**; 023 is promoted
  in the follow-up below, and 024 is unchanged.
- The strict Python full+extended gate passes **531 apps** before promotion.
  Final strict PowerShell full+extended stack/no-stack gates each pass
  **532 apps**, plus extended, diagnostics and dccpeep fixtures; normal-stack
  performance passes. Targeted full-mode promotion and host validation pass.
- Both final canonical-build censuses contain **3,487 functions**, adding
  only tpost16's five functions. Every original **3,482-function** row is
  byte-identical in both modes. All **516 existing performance rows** are
  unchanged in either direction; tpost16 alone adds a row (69,593 peep /
  70,119 nopeep cycles; 5,632-byte COM in both modes).
- `postincvalue` now steers byte postfix values only (BUG-024); word postfix
  coverage is restored. BUG-010 wide-postfix and BUG-023 cross-type guards
  remain. Integer production seeds 1-300: **227 ok, 73 INVALID, zero FAIL**.
  The final 3,000-seed remaining-known-shape proof and four generator tests
  pass. No float generator changes or float batch are needed for this fix.
- Both pipeline assemblies of the unsigned-local neighboring control are
  wholly unchanged in the same-worktree before/after guard experiment.
  The fixed `returned_increment` body is byte-identical to that control in
  both pipelines. Its exact assembled size changes **86 -> 87 bytes
  nopeep**, **80 -> 73 bytes peep**; the one-byte nopeep cost is confined
  to the previously incorrect new reproducer, not existing functions.
  No older checkout, regression dating, fallback path or prepush run.
- Canonical and independent CMake compiler builds pass; MIR host tests
  **6/6**, debugger-host tests **10/10**, focused integer/line-debug tests
  **24/24**, and strict require-emit diagnostics pass. Full-debug target
  output remains correct with nonempty metadata; line-debug COMs are
  release-identical in both pipelines. Statement/scope/type boundaries
  are unchanged. Focused ASan/UBSan compiler runs on tpost16 and bug024
  are clean.
- The full Python suite ran **318 tests**, with two built-in skips intact.
  Approved #277 golden failures remain in `test_mir_campaign_baselines`
  and `test_mir_exact_controls`; they were not fixed, skipped or refreshed.
  Under concurrent machine load, the initial run also had five errors
  across three mutation-runner methods (including teardown races), plus
  watchdog and process-supervision timing failures. The five affected
  test methods were retried separately in isolation (**5 tests, 397.7 s**):
  process-supervision's `test_post_exit_grace_preempts_long_overall_deadline`
  passes; four mutation-runner cases remain **unverified**. The default
  serial/parallel and survivor-recording methods time out after 180 s;
  exited-worker cleanup cannot find `watchdog-child`; the worker watchdog
  exceeds its 8 s bound (8.56 s). Retry teardown races disappear. These
  are harness failures, not hidden passes: no expectation, runner, fixture
  or skip was altered, and no older/main-checkout comparison was made.

#### D2a BUG-023 follow-up

- Confirmed BUG-023 **FIXED in peep/nopeep**, including host, ASan and
  UBSan baseline proof. It shares the existing selfstore-fusion root cause
  with BUG-021; it is moved from C, not treated as an incidental flip.
  Promote as `tpostbr` with the unchanged eight-line host baseline.
- Final known-bug run: **26 reproducers, 26 fails, zero FIXED**. Counts agree:
  **26 open, 27 reported fixed** in compiler-bugs.md; fix-plan.md line 3
  has 26 open. BUG-024 remains parked and failing.
- New normal-stack row: **152,995 peep / 154,692 nopeep cycles**;
  **5,888 / 6,016-byte COM** respectively. All **517 pre-follow-up
  performance rows** (including tpost16) are unchanged; tpostbr alone
  adds a row. Promoted host validation and targeted full-mode checks pass.
- Both censuses preserve all **3,487 pre-follow-up functions byte-identically**
  and add only tpostbr's nine functions (**3,496 total**). Thus all original
  3,482 functions and 516 original performance rows remain unchanged too.
- Removed `crosstypeasg` from `KNOWN_BUGS`; the cross-type rewrite is no
  longer active under `--off known`. Five focused generator tests and
  the 3,000-seed remaining-known-shape proof pass. Both strict full+extended
  stack/no-stack release gates pass **533 apps**, extended, diagnostics
  and dccpeep fixtures; stack performance has zero regressions. Full-debug
  output matches all eight baseline lines in both pipelines, nonempty DBG
  metadata is retained, and line-debug COMs match release byte-for-byte.
  Restored-cross-type production seeds 1-300: **300 programs: INVALID 34, ok 266**; zero FAIL.
  No compiler code was changed in this follow-up.

**Maintainer-approved D2 split: D2a (021/024) and D2b (010/041).**
D2a owns stored-home/value-lifetime fixes in a separate branch. D2b tests the
shared front-end hypothesis: resolve a wide update's stored scalar type once
and use that proof for both expression admission and long/float value
classification. D1's typed MIR update arithmetic is already in place; D2b
must not change MIR lowering or selection. Acceptance is recorded below.

D1 acceptance is recorded below and in Tracking. No out-of-cluster reproducer
flipped in D1.

#### D2b partial acceptance and stopped float admission

The shared front-end admission hypothesis was confirmed by the first trial:
**010/041 both FIXED**, with every other parked reproducer still failing.
The trial resolved a wide update's stored type once for both admission and
long/float value classification; MIR lowering and emission were unchanged.
However, restored float-value fuzzing exposed a separate downstream typing
fault. The maintainer's stop rule is followed: **float admission is withdrawn,
BUG-041 remains open, and only the safe BUG-010 fix ships in a normal PR**.

The final compiler change is one admission disjunct: use the already correct
`ast_value_is_long_word` proof when admitting a postfix expression. That
classifier resolves long update lvalues before calling expression admission,
so this path does not recurse. Existing direct long assignments and prefix
updates, plus byte/word/pointer/float admission rules, are unchanged.

- Initial trial: **28 reproducers, 010/041 FIXED, 26 other fails**. Its
  strict Python full+extended acceptance passes **531 apps**, extended,
  diagnostics, dccpeep and performance. Both **3,482-function** trial
  censuses are exactly byte-identical and all **516 existing perf rows**
  are unchanged. Final long-only acceptance is recorded below.
- Float seed **9** fails all four production configurations. Its 135-line
  reduction isolates a negated member-array float update: expected IEEE
  bits `c0490fd0`, actual `fffff030`, with the stored increment correct.
  Float seed **81** reduces to the same integer unary consumer after a
  float prefix update on a block-local initialized from an integer
  (expected `3f800000`, actual `00000000`). Both are host + ASan/UBSan
  proven, parked as **BUG-056 / [#299](https://github.com/davidly/dcc/issues/299)**.
  The issue preserves the admission-only proof patch, explicitly not shipped.
  The float batch was stopped after these two findings; it is not called clean.
- MIR's load/update/store are float, but the dependent unary retains AST
  `TYPE_INT`; `mir_lower_simple_unary_chain` copies that earlier type.
  D1's update repair does not repair this consumer. No second compiler
  fix was attempted. BUG-041 / BUG-056 remain behind `fincdecval` steering;
  the float trial's new classifier and its float-prefix unit test are removed.
- Narrowed long postfix emits exactly the accepted direct-assignment
  control in peep/nopeep. Permanent tests check explicit casts and narrowing
  returns, signed/unsigned long, both update directions, release, stack-check
  and line-debug. Host + ASan/UBSan probes also cover carry/borrow/wrap,
  long array/member/deref narrowing and negated long member postfix values.
- Independent CMake compiler build / MIR host checks **6/6**, focused
  compiler/debug tests **28/28**, and strict require-emit pass. Line-debug
  binaries are release-identical; full-debug metadata/output are valid in
  both pipelines. No source/statement/scope boundaries or existing locations
  move. The initial debugger-host MI endpoint cleanup assertion under load
  passes an unchanged **isolated retry**; all ten host tests are covered.
- Final long-only acceptance: **29 parked reproducers, only 010 FIXED**,
  with 041/056 rejected and all original out-of-cluster reproducers unchanged.
  After promoting `tpost32`: **28 reproducers, 28 fails**. Its unchanged host
  baseline passes validation and normal-stack full-mode measurement; the
  only new row is **95,435 / 95,464 cycles, 5,248 / 5,248 bytes** (peep/nopeep).
- Both final strict PowerShell full+extended gates pass **532 apps**, plus
  extended, diagnostics and dccpeep fixtures; stack-check performance passes.
  Final censuses have **3,488 functions**: all **3,482 existing rows** are
  byte-identical in both modes, and exactly six `tpost32` functions are new.
  All **516 existing CSV performance rows, including row order**, are
  byte-identical; only the measured new row is appended. The CSV writer's
  incidental sorting was undone without changing any measured value.
- Restored integer production seeds **1-300: 227 ok, 73 INVALID, zero FAIL**.
  Final targeted float-value steering proof passes **3,000 seeds** for the
  blocked BUG-041/056 shapes; the all-known float proof still has the unrelated
  BUG-045 gap noted below. The final blocked-float production health batch is also clean: **281 ok,
  19 INVALID, zero FAIL** (300 seeds).
- Final independent CMake compiler / MIR host checks **6/6**, targeted
  compiler/line-debug tests **16/16**, and promoted `tpost32` full/line-debug
  builds pass both pipelines. Statement/scope boundaries and existing
  register/stack locations are unchanged.
- Final integer disabled-shape proof passes **3,000 seeds**. Restoring the
  still-open float-value steering exposes an unrelated existing BUG-045
  steering gap at raw seed **1262**: the `fnegcmp` detector flags a comparison
  of two folded float arithmetic constants even with that feature disabled.
  The fuzzer classifies such repair/generated logged shapes INVALID rather
  than using them as new compiler findings. The float 3,000-seed proof is
  therefore not claimed; no BUG-045 generator/compiler fix is added here.
- **Python exceptions are not hidden:** initial full discovery (319 tests,
  including the later-withdrawn float trial unit) reports four failures and
  two errors; final full discovery (318 tests) reports four failures and five
  errors. The approved #277 campaign-baseline / exact-control golden failures
  are unchanged. The remaining failures are process-supervision timing/cleanup
  assertions; errors cover three compiler-mutation runner cases (two 180-second
  fixture timeouts, teardown directories still being written, and a missing
  watchdog-child PID file). The two built-in skips are unchanged.
- Retrying only those five non-#277 cases with installed Ninja, no other unit
  tests and no harness/expectation changes still reports two failures and four
  errors (403.7 s). No failing test was skipped/refreshed and no second compiler
  or harness fix was added. The three short timing cases are then retried one
  per fresh Python process after all long jobs finish: all three still fail
  (overall-deadline cleanup reports descendant PID 0; exit-grace timing exceeds
  its 3-second bound; exited-worker cleanup has no watchdog PID file).
  **All five non-#277 harness cases remain unverified.** Production CMake /
  MIR / debugger-host and strict application gates pass. No expectation,
  skip, timeout, harness or compiler change was made to hide these failures.
- D2a owns independent BUG-021/024 stored-home lifetime fixes. No such
  emitter/lifetime change is included here. No original out-of-cluster
  reproducer flipped. No prepush or compiler-dating checks were run.

#### D1 acceptance

- Before: 33 parked reproducers fail. After the single root fix: only
  003/044 report FIXED in peep/nopeep, with 31 still failing. After promoting
  `tinc32a` / `tflinc`: **31 reproducers, 31 fails**. Both promoted apps pass
  normal-stack full-mode measurements and host baseline validation.
- Canonical build and strict Python full+extended acceptance pass (525 apps
  before promotion). Final strict PowerShell full+extended stack/no-stack
  gates each pass **527 apps**, extended, diagnostics and dccpeep fixtures;
  normal-stack performance passes. No prepush or old-compiler checks.
- Both original **3,461-function** census TSVs compare equal before
  promotion. Final canonical-build censuses preserve every existing row
  byte-for-byte and add only seven functions from the two new apps. All
  **510 existing performance rows** are unchanged in both directions; only
  the two new measured rows are added.
- Restored `wideidxincdec` / `fincdec`: 300 integer seeds **223 ok, 77 INVALID**;
  final 300 float seeds **286 ok, 14 INVALID**; zero FAIL. Both final
  3,000-seed disabled-shape proofs pass. Float seed 219 initially exposed a
  missed existing BUG-042 guard (`-((0.0625f < 602000.0f) + 1.5f)`); reduction
  has no increment/decrement. Only that guard was refined. Its accepted
  standalone comparison and explicitly cast negation remain unsteered, and
  a fresh full float batch is clean. No second compiler fix was added.
- Independent CMake compiler build, MIR host checks **6/6**, debugger-host
  checks **10/10**, strict require-emit and focused compiler/line-debug tests
  **23/23** pass. Full-debug builds of the promoted sources retain `.DBG`
  metadata and correct output; line-debug `.COM` files match release in
  both pipelines. Statement/scope boundaries are unchanged.
- **Python exceptions, not hidden:** the initial full suite (314 tests),
  concurrent with heavy validation, had the approved #277 golden failures
  plus five scheduler/cleanup errors across three mutation-runner cases.
  Retrying the final full suite in isolation (315 tests) removes those
  errors, but reports #277's `test_mir_campaign_baselines` and
  `test_mir_exact_controls`, plus
  `test_default_serial_and_parallel_are_equal_and_bounded`: its tiny fake
  host verifier exits 139 for `global-field-vn-call-barrier` in the parallel
  run. Retrying that single case alone still fails: fixture builds exit 139
  for `call-arity` / `indirect-callee`. The production CMake compiler and
  actual MIR host tests pass. This process-harness case remains unverified;
  no runner, expectation or skip was changed. The two built-in Python skips
  are unchanged. No main-checkout/dating runs were made for the maintainer's
  pre-approved #277 exceptions.

### E. Disproved: split into E1 and E2

The single root-cause fix on `fix/cluster-e-signedness` (from main
`7901b30c`) clears **BUG-016 only**. No second independent fix is included.
BUG-004's four function-scope cases also become correct, but its nested-block
assignment still fails; BUG-013/006/028/002 do not share E1's root. E1 completes
acceptance for BUG-016, while E2 records the unfixed causes below.

### E1. Declared interpretation lost by SSA load forwarding

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-016 | #236 | Local takes the signedness of its initialiser, not its declaration |

**Confirmed root cause:** `mir_unary_is_representation_identity` can erase an
initializer's same-width signedness cast, after which `mir_promote_objects`
replaces a declared local load with the initializer's differently typed SSA
value. The replacement preserves bits, not the interpretation needed by a
later widening, float conversion, or 32-bit division/remainder.

At a promoted read with an observable type difference, retain the load as a
bit-preserving `MIR_UNARY` typed from the declaration rather than aliasing it.
Follow identity casts and typed PHIs with bounded recursion, and keep the old
alias for constants whose sign bit is clear and uses that cannot observe
signedness. This neither changes selectors nor adds a fallback. An initial
experiment retaining every stored signedness cast changed 21 existing
functions across 12 apps; a use-filtered version still changed `tldref` by
retaining a cast for loop loads that are never promoted. Both were rejected.
The final fix applies at the proven reaching-value replacement itself.

- Before promotion: 1 `FIXED`, 24 `fails` out of 25 parked reproducers.
- Promoted BUG-016 as `tinittyp`, retaining all nine original expected lines
  and adding ten documented integer/float facets and assignment controls.
  Only its new PowerShell stack-check performance row is added:
  peep 414,199 cycles / 7,424 bytes; nopeep 414,665 / 7,552.
- Restored integer `inittype` and float `finittype`. Production seeds 1-300
  finish with integer **297 ok, 2 INVALID, 1 target-limit-only** (178: TPA too
  small), float **281 ok, 19 INVALID**, and **zero FAIL** in either batch.
- BUG-004 improves from 5/5 wrong lines to only its block-local line wrong:
  forwarding a word local now keeps its typed view of an untyped boolean
  PHI. Its full issue remains open, with the remaining facet in E2/A2b.
- Out of cluster, BUG-015 improves from 10/13 wrong lines to 9/13: the
  nested-block `uint16_t --b` is now zero-extended as the declared word.
  No BUG-015 admission, pointer typing, or decrement lowering was changed;
  the complete reproducer still fails and all of its steering stays parked.
- Both existing MIR censuses are byte-identical across **3,509 functions**
  (all TSV fields equal), with zero changed apps or selected hashes.
- Final post-promotion censuses add only `tinittyp`'s 13 functions (3,522
  accepted total); all 3,509 pre-existing rows still compare identically in
  stack and no-stack modes. All 24 remaining parked reproducers report
  `fails`, with zero `FIXED` or `UNPROVEN`.
- Final strict PowerShell full+extended stack and no-stack gates both pass:
  536 apps, diagnostics, peephole fixtures and the extended suite; no existing
  performance row changed. `validate-unit-test.ps1 -App tinittyp` also passes.
- Strict Python full+extended acceptance passes: 535 apps, 196 runnable
  extended tests, diagnostics and optimizer fixtures; zero performance
  regressions and all **520 existing performance rows** unchanged.
- CMake Release, core MIR CTest (6/6), require-emit diagnostics, and isolated
  ASan/UBSan compiler checks pass. Debugger-host
  CTest passes 10/10, line-debug tests pass 2/2, and the new integer/float
  cases run correctly with full `-g` and `-gline` through both pipelines;
  `-gline` COM files are release-byte-identical.
- BUG-016's integer and documented float facets agree with optimized and
  ASan/UBSan host clang in both production modes. The fixed
  `unsigned_from_signed` emits `ld hl,(gs); ld de,0; ret`, exactly like the
  separate-assignment control in peep and nopeep, rather than sign-extending.
  Five corrected functions match their separate-assignment controls exactly;
  the long-cast remainder omits twelve redundant argument-staging instructions
  present in its control, rather than introducing a larger schedule.

- Regular Python unit gate ran all **320 tests**: **3 failures, 4 errors,
  2 existing skips**. The two approved #277 failures are
  `test_campaign_assembly_baselines` and
  `test_campaign_positive_controls_select_their_exact_schedules`.
  Four synthetic PowerShell mutation-fixture cases had timing/process errors:
  `test_child_timeout_and_environment_isolation` (diagnostic lost),
  `test_default_serial_and_parallel_are_equal_and_bounded` (180-second timeout
  plus teardown race),
  `test_survivors_invalid_exits_builds_and_worker_errors_are_recorded`
  (180-second timeout), and
  `test_worker_watchdog_records_invalid_and_stops_descendants` (child PID
  file not created). All four were retried in isolation without changing
  tests: watchdog passes; the other three still fail (**1 failure, 4 errors**
  including both timeout teardown races). These synthetic fixtures do not
  execute the changed compiler. Per the pre-answered harness-timing policy,
  no test, timeout, expectation or skip was changed; those three harness
  checks remain unverified. Logs: `build/e-python-units.log` and
  `build/e-python-isolated.log`. No historical compiler/main check was run.

### E2. Remaining expression/conversion defects (independent of E1)

| Bug | Issue | Traced cause / fix location |
| --- | --- | --- |
| BUG-013 | #233 | `ast_expr_type_for_sizeof` in `dcc_ast_build.c` recursively retypes a nested binary from raw field types instead of its recorded promoted result; unary `-`/`~` and `?:` also use raw unsigned field types. `p_binary` already gives direct narrow bit-field binaries signed operand types, but an enclosing expression loses that result type. Field-aware promotion must be consistent across those AST typing paths. |
| BUG-006 | #226 | `mir_lower_expr_impl`'s `AST_ASSIGN` member compound path builds its initial `MIR_LOAD_INDIRECT` with `node->a->type == 0`, despite having the resolved `FieldDef`. `mir_emit_bitfield_extract` interprets missing `TYPE_UNSIGNED` as signed, so the top bit sign-extends. Repair explicitly excludes bit-field loads from pointee inference. Preserve the field's declared extraction type separately from its promoted arithmetic type here. |
| BUG-028 | #248 | `mir_resolve_deferred_metadata`'s general binary widening loop promotes a discarded byte compound shift to the wider **right/count** type unless `deferred_operand_types` is set. An unsigned word count therefore stamps `TYPE_UNSIGNED` onto a signed-byte `>>=`. The shift computation belongs to the promoted left operand in `mir_lower_compound_value` and that repair loop, not to the count. |
| BUG-002 | #223 | `mir_lower_compound_value` keeps discarded byte shifts typed as the stored byte. With a byte count no width change forces an explicit truncation; the result's nominal byte type survives object promotion although the emitted word shift holds high bits. A following byte return reuses that value without masking. The promotion/narrowing contract belongs in byte compound-shift lowering (and its store-result handling), not the initializer signedness fix. |
| BUG-004 (remaining block facet) | #225 | `mir_lower_ident_assignment_conversion` sees an unresolved block lvalue type and does not type the boolean PHI. `mir_resolve_deferred_metadata` later restores the conversion target to long, but its source PHI stays untyped; conversion emission therefore leaves the high word uninitialized. Extend `mir_type_untyped_widened_value`'s contract to repaired canonical assignment conversions. The function-scope half is fixed incidentally by E1; A2b may proceed on this explicit remaining facet, not an assumed BUG-015 shared cause. |

BUG-002 and BUG-028 are a promising byte-shift subgroup: discarded byte
compound shifts lack promoted-left computation and a distinct narrowed result;
BUG-028 additionally exposes the count-driven repair. That grouping is traced,
not implementation-proven: no second fix or E2 experiment was attempted.

No E2 implementation is included. All five complete reproducers still fail,
and their generator `KNOWN_BUGS` entries remain enabled.

### F. Compound assignment to complex lvalues

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-011 | #231 | `op=` on an element of a byte array inside a struct: rejected |
| BUG-018 | #238 | `op=` of a 32-bit value to a bit-field or to an element of a 16-bit array member: rejected |
| BUG-043 | #263 | `op=` on an integer lvalue with a float right operand: rejected |

**Confirmed shared root cause:** stale AST admission gates enumerate legacy
lvalue/RHS restrictions although the existing MIR numeric compound contract
already loads, converts, computes, narrows and stores all three shapes.
One shared integer-compound admission rule validates the existing lvalue proofs
and numeric operand/operator types. Upstream `34e35b3a` already supplied the
lowering; no lowering, allocation, selection, emitter or runtime change is
needed. All three bugs share this cause, so no F1/F2 split or second compiler
fix is necessary.

- Before promotion: 35 reproducers give three `FIXED`, 32 `fails`; only
  BUG-011/018/043 flip. BUG-055/#294 is an independent restored-float finding
  with no integer compounds, parked and narrowly steered, not fixed here.
- Both censuses are byte-identical across all 3,436 existing functions,
  including hashes; all 509 old perf rows retain their exact bytes. New
  byte-member and integer/float sequences match already-supported controls.
- Restored integer features: 213 ok, 85 INVALID, two target-limit-only,
  zero FAIL for production seeds 1-300. Restored `icasgf`: final float batch
  256 ok, 44 INVALID, zero FAIL after triaging seed 241; 3,000-seed steering
  proof clean. The discovery batch was 255 ok, 44 INVALID, one BUG-055 FAIL.
- Promoted `tcmbyte`, `tcmwide`, `tcmfloat`, with three newly measured normal-
  stack PowerShell rows. Strict stack/no-stack full+extended, Python runall,
  host/sanitizer baselines, native MIR, debugger/metadata and require-emit
  gates pass.

The final regular Python gate runs 314 tests with the two approved #277
failures and two existing skips. It also hits the unchanged mock mutation
fixture's 180-second deadline in
`test_default_serial_and_parallel_are_equal_and_bounded`, then a directory-
not-empty cleanup error (two errors). The same fixture timed out in two earlier
runs; its scripts and manifest are byte-unchanged and it builds a synthetic
mock compiler, not the modified dcc sources. The #280 fixture's final assertion
is therefore unverified on this host. No compiler/harness fix, skip, expectation
refresh, historical compiler check or prepush run was made for these failures.

### G. Constant expressions in the front end

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-012 | #232 | Unary `-`/`~` on `!`, `&&`, `||` of constants: rejected |
| BUG-033 | #253 | Constant divided by constant zero in dead code: rejected |
| BUG-040 | #260 | Static float initialiser must be a bare constant |
| BUG-042 | #262 | Float constant that must be folded or truth-tested: rejected |

The shared-cause hypothesis is disproved: the first root-cause fix clears
only BUG-012 (**G1**); BUG-033/040/042 remain **G2**. Per the approved stop
rule, no second independent fix is included.

**G1 — integer unary constant-expression admission.** The AST support gate
already admits `!` when `ast_const_scalar_fold()` proves the expression, but
rejects `-`, `+` and `~` over constant logicals or nested binary/unary forms.
Apply that existing foldability proof to all four unary operators, preserving
all preceding fast-path gates. MIR already lowers and folds these operations;
no parser, folder, MIR, emitter or runtime changes are needed. BUG-012's
original functions now emit byte-identically to their accepted literal
neighbors, with no instruction or frame cost.

**G2 — separately traced, not fixed:**

- **BUG-033:** `ast_const_plain_int_binary_supported()` explicitly declines a
  constant zero divisor; the integer folder also correctly declines its
  evaluation. The generic constant-binary gate then rejects the expression.
  A future fix must admit and retain the unevaluated `/` or `%` operation,
  as clang does, never fold it to a made-up value. The variable-numerator
  neighbor uses the nonconstant arithmetic path.
- **BUG-040:** `parse_global_init()` and aggregate initializers call
  `parse_float_init_literal()`. Its compact token parser accepts a bare signed
  literal or the narrow cast/multiply/divide form, not parentheses and general
  float constant arithmetic. This is independent of function-body AST
  admission; automatic initializers can instead reach AST/MIR capture.
- **BUG-042:** `ast_const_scalar_fold()` has no floating-literal result and
  its binary evaluator is integer-only. Constant unary/logical gates need
  that fold proof, while `ast_cond_generic()` explicitly excludes constant
  floats from its runtime truth-test path. Float comparisons alone have their
  own condition gate, explaining the accepted neighbors. Extending the
  integer unary admission proof cannot admit these float forms.

G1 acceptance: **1 FIXED, 33 fails** before promotion, no out-of-cluster
flips. Integer `negnotconst` is restored; production seeds 1–300 give
**216 ok, 82 INVALID, two target-limit-only, zero FAIL** in all four
configurations, and the remaining disabled-shape proof covers 3,000 seeds.
BUG-012 is promoted as `tconstun`, including constant logicals, the six
nested unary/binary facets, and variable/single-unary controls. Host and
ASan/UBSan stdout proof, host validation, and normal-stack performance
measurement pass; only its new performance row is added.

All **509 existing performance rows are unchanged**. The ordinary census
retains **3,436 existing functions byte-identically in each mode**; the
extended census retains **274 functions in each mode** (548 rows), with
zero hash or metric changes. Strict Python full+extended and PowerShell
full+extended stack/no-stack runs pass. CMake compiler build and focused
line-debug tests pass. The original fixed shape has release-identical
`-gline` bytes and intact full-debug locals/frame locations. After promotion,
all **33 remaining reproducers fail**, and only the new app adds four
ordinary-census functions per mode. Full debugger-host tests pass **10/10**;
the first run's unrelated terminal endpoint-cleanup assertion passed on
its isolated retry and the subsequent complete run, without changes.

The full Python gate ran **312 tests** with the two approved pre-existing
#277 failures (`test_mir_campaign_baselines`, `test_mir_exact_controls`),
two existing skips, and a 180-second timeout in
`test_default_serial_and_parallel_are_equal_and_bounded`. An unchanged
isolated retry completed its serial phase but saw two mock host-verifier
exit-139 results in its parallel phase (the #280 fixture pattern: mutations
`preprocessor-character-hex` and `phi-consumer-value`). The mixed-outcome
fixture passed in the full run. These mock compilers build only the test's
synthetic `mir_verify.c`, never the changed AST module; no test expectations,
limits, skips, fixtures or production runner code were modified. The same complete serial/parallel fixture passes unchanged with the installed
Ninja generator. The Makefiles timeout/exit-139 instability is retained as an
out-of-cluster observation, not hidden or fixed. No historical compiler
checks or `prepush.ps1` run were performed.

### H. Comparison with a compound right operand on a 32-bit or float parameter

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-020 | #240 | After `switch`/`if`+loop, `v REL param * / % k`, or `7.0f != (x0 + 28)`: `MIR emission is required` |
| BUG-045 | #265 | `x REL (-C OP y)` with a negative float constant: `MIR emission is required` |

Both are comparisons whose right side is an arithmetic expression over a
parameter, both fatal with no candidate able to emit, and both pass with the
operands swapped or the expression hoisted into a temporary. Look at how the
comparison selector accepts a non-leaf right operand.

### I1. Long ↔ float return admission

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-046 | #266 | `return` does not convert between float and 32-bit integer |

**Confirmed root cause:** `ast_return_stmt_supported()` rejects long
expressions in float-returning functions and float expressions in
long-returning functions before MIR lowering. `mir_lower_stmt()` already
performs the required scalar return conversion. Admit the missing operand
classes without changing lowering, selection, allocation, or emission.

The original cluster I hypothesis was disproved: this root fix made only
BUG-046 `FIXED`; BUG-048 retained 5/6 wrong lines and BUG-050 retained 3/4.
That experiment was reverted and recorded in draft [#285](https://github.com/davidly/dcc/pull/285).
The maintainer approved the I1/I2 split and resuming I1 on `fix/cluster-i`.
BUG-046 is now fixed and promoted as `tests/tfltret.c`.

In both peep and nopeep, the complete fixed BUG-046 `.MAC` is byte-identical
to the explicit-return-cast control, whose output is unchanged before/after.
`float a(int32_t x)` loads HL/DE and calls `__flf`; `int32_t d(float x)`
similarly calls `__ffl`. No fallback or larger schedule is needed.

Acceptance: before promotion, **1 FIXED, 41 fails**, with no out-of-cluster
flips; after promotion all 41 remaining parked reproducers still fail.
Strict Python full+extended and both PowerShell stack/no-stack release gates
pass. Both censuses are byte-identical across 3,366 existing functions.
Restoring `fretconv` gives **265 ok, 35 INVALID, zero FAIL** on production
float seeds 1-300. The host baseline is unchanged; only the new `tfltret`
performance row is added. CMake, debugger-host tests, focused line metadata,
release-identical `-gline`, and full `-g` output checks pass.
The final Python gate ran 308 tests: the two approved #277 failures and
two skips. A concurrent mutation-fixture timeout cleared on a sequential
focused rerun; the nondeterministic #280 exit-139 failure did not occur.
Nothing was skipped or refreshed to accommodate those known failures.

The final fuzz proof also corrects the float shape checker's contract to
report only shapes in `prog.off`, matching the integer checker. Restored
`fretconv` must not be treated as a logged-shape INVALID after its steering
entry is removed. A focused regression covers restored return and cast
features; the superseded 258-ok/42-INVALID batch excluded seven return-shape
programs. The corrected batch executes them and gives 265 ok, 35 INVALID.

### I2. Cast signedness before conversion to float

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-048 | #268 | `(uint32_t)x` to float, `x` signed 32-bit: signed conversion |
| BUG-050 | #270 | `(int32_t)(uint32_t)x` to float, `x` signed 8/16-bit: unsigned conversion |

**Confirmed shared root cause:** `mir_unary_is_representation_identity()`
preserves an equal-width integer signedness cast before a wider conversion,
but not before an equal-width long-to-float conversion. Removing the cast
makes `mir_emit_cast()` correctly choose a helper for the wrong source type:
`__flf` instead of `__fulf` for 048, and the reverse for 050. The root fix
retains casts feeding float conversions as well as widening conversions.

The first fix clears both reproducers in peep and nopeep: **2 FIXED,
40 fails**, with no out-of-cluster flips; I1's BUG-046 still fails on this
independent branch. Both existing-function censuses are byte-identical
across 3,366 functions. The complete parked programs' assembly changes only
conversion helper calls and associated `extrn` placement, not instructions
around them or their existing correctly typed controls. The logged 050
folded/arithmetic and global/local/parameter facets also match the sanitized
host oracle in both modes.

Retaining the cast emits no value-changing instructions. An isolated
zero-local parameter-only microprobe does retain an extra `ld sp,ix`
(2 bytes, 10 Z80 T-states) compared with its already-typed control; no
existing function changes and neither parked reproducer gains that
instruction. No general/spilled fallback was introduced.

This work uses `fix/cluster-i2-float-conv`, independently based on `origin/main`
`0d11922d` in its own worktree, not I1. Both subclusters remain independent
of A–E. BUG-048/050 are promoted as `tflucast`/`tflscast`. Strict Python
full+extended and both PowerShell stack/no-stack release gates pass; both
host validations and targeted peep/nopeep tests pass. All 500 existing
performance rows are preserved verbatim, with exactly two new rows.
Corrected production float seeds 1-300 give **267 ok, 33 INVALID, zero FAIL**;
all 40 remaining parked reproducers still fail.

The float shape checker also needed a directly coupled acceptance fix:
return only shapes in `prog.off`, as the integer checker does. Otherwise
restored `fu32cast`/`fretconv` forms were excluded as INVALID. The first
140-ok/160-INVALID batch is superseded, not claimed as restored coverage.
A focused checker regression protects both features; I1 receives the same
checker fix independently. MIR/line tests, CMake, ASan/UBSan, debugger-host,
release-identical `-gline`, and runnable full `-g` checks pass.
The full Python gate ran 308 tests with the two approved #277 failures and
two skips; the new checker regression passes separately. Concurrent
mutation-fixture timeout/cleanup errors all cleared on sequential focused
reruns (3/3 pass). The known #280 exit-139 fixture did not recur on those
reruns; no fixture, skip or expectation was modified.


### J. Float arithmetic

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-047 | #267 | `f + (x * y)` drops the product (two factor-kind families) |
| BUG-049 | #269 | `!f` tests raw bits: `-0.0f` is true |
| BUG-039 | #259 | `__fdiv` is 1 ulp off in about a fifth of cases (runtime, `DCCRTL.MAC`) |

Three separate fixes; grouped only because they share the float test
harness. BUG-047 is the maintainer's third priority. BUG-039 is a runtime
change: keep it small, follow the existing `DCCRTL.MAC` idioms, and run
`scripts/rtl-iy-safety.py` and `scripts/audit-runtime-coverage.py`.

Cluster J investigation (`fix/cluster-j-float-arith`):

- BUG-047: the wide register-forwarding proof targets a multiply whose
  emission is suppressed by multiply-add fusion. The fused add loads its
  addend before its factors, clobbering the forwarded factor; its reload
  then reads a slot that was never allocated or written. Declining only
  that invalid handoff fixes the reproducer, retaining fusion. Both MIR
  censuses preserve all **3,394 existing functions** byte-for-byte.
- BUG-049 is **parked under the unchanged-output rule**. Using the same
  sign-masked float zero test as the branch emitter fixes the reproducer,
  but necessarily changes `tctxflt:truth_not`, `tctxflt:truth_not_memb` and
  `tnestfor:nz_float` in both censuses (one extra `and 127`). That experiment
  was reverted; no value-origin or function-name exception was added.
- BUG-039 is **parked under the unchanged-output rule**. The runtime rounds
  its 24-bit quotient before normalizing ratios below one, losing a bit
  before rounding. An extra restoring-division step before rounding fixes
  all 404 parked results and reduces the helper's static code, but changes
  existing stdout in `tfdedge`, `trig`, `tstrb11`, `ttrg6sp` and `ttrig`
  (both modes), as well as division-user cycles. The runtime experiment
  was reverted; no output/performance baseline was refreshed. Both runtime
  audits passed after the edit and its reversal. A linked probe through
  `__fdf` and the stack ABI `__fdiv` matched 804 host bit patterns in each
  mode. This needs explicit approval to update existing numeric
  expectations, not a narrower value-specific runtime patch.

Cluster J2 investigation ([PR #291](https://github.com/davidly/dcc/pull/291),
`fix/cluster-j2-float-neg-zero-fdiv` from `1a193976`;
maintainer-approved independent fixes and baseline exception):

- BUG-049: logical negation in the spilled emitter reduced all four raw
  bytes, unlike float branches and bool casts, which mask the sign byte.
  Using that same `and 127` test fixes negative zero without changing
  integer negation or candidate selection. Both censuses compare **3,426
  existing functions**: only `tctxflt:truth_not`, `tctxflt:truth_not_memb`
  and `tnestfor:nz_float` differ, each by exactly one `and 127`. Removing
  that instruction from each selected stream reproduces its before-census
  hash, in both stack modes. The approved stack-check performance updates
  are `tctxflt` 365581/371984 -> 365602/372005 and `tnestfor`
  146931/182071 -> 146959/182099 (peep/nopeep); linked sizes are unchanged.
- Before promotion, BUG-049 alone flips: **1 FIXED, 35 fails**, including
  BUG-039 still failing. Restored `fnegzero_not` float seeds 1-300 in both
  production modes give **269 ok, 31 INVALID, zero FAIL**. It is promoted
  as `tflnot` with a new stack-check performance row and host-proven
  baseline; no existing stdout baseline changes.
- BUG-039 remains **blocked by the new host-output proof requirement**,
  not by the unchanged-output rule. The smallest restoring-step experiment
  adds a 25th division iteration for an unnormalized quotient before
  rounding, reusing `FDLUP` and eliminating the post-round normalizer.
  It fixes all 404 parked outputs, and a linked probe explicitly calling
  both `__fdf` and stack-ABI `__fdiv` matches 808 host output lines in each
  mode (404 quotients through each ABI). Both runtime audits pass.
- All five predicted runtime stdout changes occur in both modes, but none
  of their complete replacement baselines can be proved by host clang.
  For example, `tfdedge` would print `14.285715` and `-5.666666`; clang
  prints `14.285714` and `-5.666667` with the identical `%f` formats and
  float arithmetic, at `-O0`, `-O2` and under ASan/UBSan. This also exposes
  dcc's independent float-formatting differences. `trig` still differs
  through approximate runtime math (`tanf(1)` 1.557448 vs host 1.557408).
  `tstrb11`'s integer cancellation becomes 3f800002, while host parsing
  gives 3f800000, and its target-only flush-to-zero/errno expectations
  differ. `ttrg6sp`'s new half-pi tangent CBAE8A4B differs from host
  CBAE8A4A, as do its intentional target cutoff checks. Host `ttrig`
  builds after merely renaming its conflicting local math functions;
  its changed tangent lines agree, but its complete stdout still differs
  in other formatted values. Host probes retain single-precision float
  expressions and the original printf formats; the parsing union is
  narrowed to 32 bits for host portability. No baseline is refreshed.
- The runtime experiment was reverted and both runtime audits rerun;
  `DCCRTL.MAC`, BUG-039's reproducer, `fdivround`, division-user perf rows,
  and all existing stdout baselines remain unchanged. Deliver BUG-049
  independently; BUG-039 needs a separate policy for target-specific
  numeric tests/formatting, not a value-specific divider patch.

J2 acceptance: after promotion, **35 reproducers, 35 fails**. Strict
full+extended stack and no-stack gates each pass **523 apps**, with 24
existing skips, diagnostics, optimizer fixtures and the extended suite
passing. The final censuses add only `tflnot:main` beyond the three
approved existing-function deltas. The new `tflnot` stack-check row is
37352/37758 cycles and 5632/5760 COM bytes (peep/nopeep); host baseline
validation passes. Release, `-gline` and full `-g` pipelines pass in both
peep modes; release/`-gline` COM files are identical, and all 10 debugger
host tests pass. Canonical and independent CMake builds pass.

The Python gate runs 312 tests with **2 failures, 2 errors and 2 existing
skips**: the known campaign/exact-control failures (#277), the known
exit-worker cleanup fixture error (#280), plus a 180-second serial
mutation-fixture timeout. The latter reproduces in isolation and with
unchanged `1a193976` scripts archived under this worktree's `build/`:
only its tiny fake-tool scheduler fixture is rebuilt, not an older dcc.
No failing test is fixed, skipped or refreshed; the full Python gate
remains red. No prepush or compiler regression-dating check is run.

### K. dccpeep

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-032 | #252 | `x + K` (2–255) dropped when the sum is copied to another slot |
| BUG-034 | #254 | `8 == v` becomes a bit-3 test |
| BUG-035 | #255 | `a > x || x < k` after a stack struct member compares `x` with itself |
| BUG-053 | #273 | `pass_elim_dead_ix_stores` forgets an escaped address at its own `LS<n>:` label |

Four unrelated peephole rules, each with a precise description in its entry.
Fixed in [#278](https://github.com/davidly/dcc/pull/278), with the maintainer's explicit approval
to keep all four independent fixes together. Regressions are now
`tpeepsum`, `tpeeq8`, `tpeerng`, and `tpeesc`. The cp8 proof also required an
approved `dccmake`/`dccpeep` closed-program contract, restricted to single-source
applications, to preserve the existing `ttt` schedule without guessing public
parameter bounds from per-module callers. All existing performance rows and
compared corpus assembly are unchanged, apart from `tstdc`'s embedded `__TIME__`.

Both production regression modes, 72 optimizer fixtures (also sanitized),
debugger/metadata checks, and 300 seeds per affected generator passed. Five
Python failures reproduce identically on a clean, rebuilt `main` at
`d6586aa0`; the sixth test's initial timeout did not recur on `main` or in an
isolated branch recheck. The maintainer approved documenting unrelated
Python-gate failures without changing their baselines or skipping them.
At the maintainer's later request, the two macOS harness fixes from
[#276](https://github.com/davidly/dcc/pull/276) are also included. Any remaining
pre-existing Python-gate failures should be fixed separately.

### K2. Live HL deleted by a deferred global reload

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-054 | #288 | `pass_defer_global_push_reload` removes the load that still supplies a byte self-operation's resident HL operand |

Found while verifying B3, integer seed 255; independently reproduced on
unchanged current main in peep and peep-nosc, with nopeep correct. This was
parked separately from B3's compiler repair.

Fixed on `fix/cluster-k3-peep-live-reg`, with acceptance PR
[#298](https://github.com/davidly/dcc/pull/298). Deferral now proves that the original
H and L are dead after the removed push, including beyond the matching pop.
The shared CFG proof alone conservatively rejected `tcaslv`'s safe split
sign extension because accumulator rotates are opaque there; a local
byte-kill scan recognizes only accumulator rotations while preserving
unknown/user-assembly barriers and leaving shared effects unchanged.

BUG-054 is promoted as `tpephl`; it passes in both stack modes and both
pipelines, and all 29 other parked reproducers remain unchanged. Removing
`peepbyteself` from integer `KNOWN_BUGS` restores production seeds 1-300:
**228 ok, 72 INVALID, zero FAIL/target limits** across peep/nopeep and
stack/no-stack. No float steering is affected.

Strict full+extended suites pass in both stack modes: 529 runnable apps,
24 existing ignored apps and 196 runnable extended tests. Whole-suite
before/after comparisons preserve **3,047 generated MAC files per stack
mode**, apart from `tstdc`'s embedded `__TIME__`. All **514 existing perf
rows**, including the 509 requested rows and five already present on
`4bba5188`, are unchanged. Only the new test's measured normal-stack row
is added. All 72 existing optimizer checks plus the new nine-shape fixture
pass, also sanitized. `.COM`/`.DBG` pairs and release-identical `-gline`
code pass both pipelines; all ten debugger-host checks pass. No MIR or
compiler emission changes, baseline refreshes, historical compiler checks
or `prepush.ps1` run.

The final full Python gate ran **314 tests: 310 passed, two existing skips,
and only the two approved #277 failures** (`test_mir_campaign_baselines`,
`test_mir_exact_controls`). Neither expectation was changed. The initial
loaded run also had five mutation-harness timing failures: four passed an
isolated Ninja retry, while the default fixture's parallel mock CMake build
then hit exit 139 (#280). All five pass in the final full Ninja run;
no harness code was changed or skipped. The byte-steering unit tests now
explicitly disable the optional feature and exercise self-operands with
`--off known`; their old open-bug assumption was updated with the closure.
Pre-fix corpus runs passed apps, performance and extended tests; their
expected failure was solely the new negative optimizer fixture added
while those captures were running. No out-of-cluster compiler defect
flipped or was newly confirmed; only the approved #277 Python gate remains
non-green.

### L. Preprocessor

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-051 | #271 | Macro parameter named like a constant's suffix is substituted inside the constant |

The number lexer stops before the suffix letters. One fix in
`src/dcc/dcc_preproc.c`. Isolated.

Fixed on `fix/cluster-l-pp-numbers`: both macro text walkers now copy complete
pp-numbers before identifier substitution. Regression `tppsuf` has the
unchanged host baseline and one new measured performance row; all 495 existing
rows and compared corpus assembly are unchanged (apart from `tstdc`'s
`__TIME__`). The other 46 reproducers still fail. There was no BUG-051 steering
entry to remove; both unchanged generators passed 300-seed production batches.
Strict full+extended stack-check and no-stack-check runs and debug metadata
checks passed. The two regular Python-test failures match clean rebuilt
`main` at `637500a7` exactly (#277); expectations were not changed.

### M. Singletons

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-009 | #229 | Nested `do … while (d < n)` with 32-bit `d` never exits |
| BUG-022 | #242 | Reused label name in a nested block hides `MIR emission is required` for later functions |

BUG-022 is a diagnostic bug that turns a compile error into broken output,
so it matters more than its size suggests. Both are one-offs.

## Suggested order

1. **K, L** — isolated, cheap, four and one issues; good warm-up for the
   acceptance loop.
2. **A1** — merged (#281). **A2a** — fixed and verified.
   **A2b** is deferred until after E.
3. **B1** (BUG-030 memory corruption), **B3** (BUG-026, maintainer's first
   priority), then remaining **B2b**, **B4**; **B2a** is verified separately.
4. **D** — BUG-044 and BUG-003.
5. **E**, then **A2b**, then **C** — E and C are in the emitter's
   typing/merge paths; A2b waits for E's BUG-004 facet; C is the least
   certain cluster.
6. **I1**, then **I2** on an independent branch; **J, H, G, F, M** in any order.

## Tracking

| Cluster | Branch / PR | Status | Notes |
| --- | --- | --- | --- |
| A1 | [#281](https://github.com/davidly/dcc/pull/281) | merged | 007/025/031 promoted; 300 seeds clean; existing output/baselines unchanged; approved pre-existing Python failures #277/#280 |
| A2 | `fix/cluster-a2-block-typing` (no PR) | stopped; split | First root fix cleared only 008; 004/015 still failed; experiment discarded |
| A2a | [#283](https://github.com/davidly/dcc/pull/283) | PR open | BUG-008 promoted as `tblkneg`; 300 seeds clean; existing output/baselines unchanged; Python failures match clean main (#277) |
| A2b | | open; E traced | BUG-004 block assignment still untyped at repaired widening; function facet fixed by E1; BUG-015 pointer/byte facets independent |
| B | `fix/cluster-b-stack-homes` (no PR) | stopped; split | First root fix cleared only 030; 014/019/026/036 unchanged; experiment rejected for six existing-function changes |
| B1 | [#286](https://github.com/davidly/dcc/pull/286) | merged | BUG-030 promoted as tforhome; 300 seeds clean; 500 existing perf rows and 3,366 existing census functions unchanged; strict stack/no-stack pass; approved #277 failures only (#280 fixture passes) |
| B2 | `fix/cluster-b2-home-fatals` | stopped; split | First root fix clears only 014; separate traces disprove shared cause |
| B2a | [#292](https://github.com/davidly/dcc/pull/292) | PR open | BUG-014 promoted as twincg; 300 integer seeds clean; 3,426 census functions and 507 perf rows unchanged; approved #277 failures only (#280 passes) |
| B2b | | open | BUG-019 unreachable nested-scope metadata skips declaration replay; untouched |
| B3 | [#289](https://github.com/davidly/dcc/pull/289) | merged | BUG-026/017 same root; tselfop/tandself promoted; int 213 ok/85 INVALID/2 limit-only, float 267 ok/33 INVALID; zero FAIL; 504 existing perf rows and 3,394 census functions unchanged; new BUG-054/#288 parked separately |
| B4 | [#293](https://github.com/davidly/dcc/pull/293) | PR open | BUG-036 promoted as tlpcall; C/D independent; both 300-seed batches clean; 3,436 existing functions and 509 perf rows unchanged |
| C | | open | |
| D | | split after first root fix | D1 fixes typed update arithmetic (003/044); D2 contains independent value-lifetime/admission defects (021/023/024/010/041); traces above |
| D1 | [#297](https://github.com/davidly/dcc/pull/297) (`fix/cluster-d-postinc`) | PR open; verified compiler gates | 003/044 promoted as tinc32a/tflinc; int 223 ok/77 INVALID, float 286 ok/14 INVALID, zero FAIL; 3,461 existing functions byte-identical in both censuses and 510 existing perf rows unchanged; only BUG-042 steering refined outside cluster; strict release gates pass; approved #277 plus one isolated mutation-harness process failure documented |
| D2 | | split into D2a / D2b | Stored-home versus AST admission are independent |
| D2a | [#300](https://github.com/davidly/dcc/pull/300) (`fix/cluster-d2a-postinc-home`) | merged | 021/023 promoted as tpost16/tpostbr; 023 moved from C, same root; 024 remains independent (D2c); 3,487 pre-follow-up functions and 517 perf rows unchanged |
| D2b | [#301](https://github.com/davidly/dcc/pull/301) (`fix/cluster-d2b-postinc-admit`) | PR open; verified partial fix | 010 promoted as tpost32; 041 admission withdrawn after new BUG-056/#299; int 227/73, blocked float 281/19, zero FAIL; 3,482 existing functions and 516 perf rows byte-identical; strict release gates pass; #277 plus five isolated harness exceptions documented |
| E | [#306](https://github.com/davidly/dcc/pull/306) (`fix/cluster-e-signedness`) | split into E1 / E2 | Single SSA forwarding fix clears 016; 004 function facet and 015 word-widening facet improve, complete issues still open |
| E1 | [#306](https://github.com/davidly/dcc/pull/306) (`fix/cluster-e-signedness`) | PR open; verified compiler gates; documented harness exceptions | 016 promoted as tinittyp; int 297/2/1 limit, float 281/19, zero FAIL; 3,509 functions and 520 perf rows unchanged; #277 and three isolated synthetic-fixture timing failures |
| E2 | | open | 013 AST promotion; 006 field extraction type; 028 count-driven repair; 002 byte truncation; 004 repaired boolean conversion |
| F | [#296](https://github.com/davidly/dcc/pull/296) | PR open | 011/018/043 promoted; single AST admission fix; 3,436 existing functions and 509 perf rows unchanged; int 213/85/2 limits, float 256/44, zero FAIL; independent BUG-055/#294 parked and steered |
| G | [#295](https://github.com/davidly/dcc/pull/295) | split; G1 fixed | First root fix clears 012 only; G2 033/040/042 separately traced, untouched |
| G1 | [#295](https://github.com/davidly/dcc/pull/295) | PR open | 012 promoted as tconstun; 216 ok/82 INVALID/2 limit-only; 509 perf rows and both censuses unchanged; approved #277 failures; unchanged mock fixture passes Ninja retry; no MIR/emitter changes |
| G2 | | open | 033 zero-divisor admission; 040 static float token parser; 042 float folding/truth gates |
| H | | open | |
| I1 | [#285](https://github.com/davidly/dcc/pull/285) | merged | BUG-046 promoted as tfltret; 300 float seeds clean; existing assembly/perf rows unchanged |
| I2 | [#287](https://github.com/davidly/dcc/pull/287) | PR open | 048/050 promoted as tflucast/tflscast; 300 float seeds clean (267 ok, 33 INVALID); existing assembly/perf unchanged |
| J | [#290](https://github.com/davidly/dcc/pull/290) | merged; partial fix | 047 promoted as tflmadd; 300 float seeds: 269 ok, 31 INVALID; existing output unchanged; 049 fixed in J2, 039 still blocked by host-output proof |
| J2 | [#291](https://github.com/davidly/dcc/pull/291) | PR open; partial fix | 049 promoted as tflnot; 300 float seeds 269 ok/31 INVALID/zero FAIL; only three one-instruction census changes and two approved perf rows; 039 reverted because all five complete numeric baselines fail host-clang proof |
| K | [#278](https://github.com/davidly/dcc/pull/278) | merged | Four approved independent rules; promoted tests and new performance rows; existing baselines unchanged |
| K2 | [#298](https://github.com/davidly/dcc/pull/298) | PR open | BUG-054 promoted as tpephl; 228 ok/72 INVALID/zero FAIL; all 3,047 MAC files per stack mode and 514 existing perf rows unchanged (except clock literal); #277 and isolated harness retries documented |
| L | [#279](https://github.com/davidly/dcc/pull/279) | PR open | BUG-051 promoted as `tppsuf`; existing baselines and corpus assembly unchanged |
| M | | open | |

Update this table as clusters are taken, and move bugs between clusters when
a fix shows the hypothesis was wrong.

## Appendix: prompt for a fixing agent

Give this to the agent once per cluster. Change only the cluster identifier in
the **Task** line.

````text
You are fixing a cluster of logged compiler bugs in dcc, a C compiler that
emits Z80 assembly for CP/M 2.2. Use the `dcc-project` and
`clang-oracle-testing` skills, and the `mir-migration` skill if you touch
MIR selection, schedules, or allocation.

## Task

Fix cluster **B1** in `fix-plan.md`. Read the cluster's table, then every
`## BUG-NNN` entry it lists in `compiler-bugs.md`, including the bullets
describing which neighbouring shapes are already correct — those are your
strongest clue to the root cause and to what the fixed code should emit.
The parked reproducers are in `tests/known-bugs/bugNNN.c` with host
baselines in `tests/known-bugs/baselines/`.

Work on a new branch from `main`, one branch for this cluster only.

## Approach

- Expect root-cause work, not pattern-matching. Trace the failing shape
  through the MIR pipeline: lowering in `src/dcc/dcc_mir.c`, the general
  emitters `dcc_mir_homed_cfg.c` and `dcc_mir_spilled_cfg.c`, selection and
  cost policy in `dcc_mir_select.c`, and the exact schedules in
  `dcc_mir_machine_*.c`. Front-end typing lives in `dcc_func.c`,
  `dcc_decl.c` and `dcc_ast_*.c`; the peephole optimizer in `src/dccpeep/`;
  the preprocessor in `dcc_preproc.c`. Find where the broken shape and the
  neighbouring correct shape diverge, and fix at that point.
- Work from the assembly. Before changing anything, compile the reproducer
  and its neighbouring correct shape with `./dccmake ... dcc-peep=false`
  and `dcc-peep=true`, and read the `.MAC` output for both. Repeat after
  the fix: the broken shape should now emit what the correct shape emits,
  with no change to the correct shape. Use `DCC_MIR_SELECT_CANDIDATE=<name>`
  to force an emitter when isolating which candidate is wrong
  (`python3 scripts/clang-oracle-fuzz.py --list-configs` lists them).
- The easy fix is the wrong fix here. Routing the shape to a general or
  spilled path will pass the reproducer and fail acceptance on the baseline
  rule. If the only correct fix costs code in the new tests, say so in the
  PR; it must cost nothing in the existing ones.
- Budget for the verification loop; it is the larger part of the task.
  Run long steps (runall, fuzz batch) as async shell commands logging to
  `build/` and read the logs; do not run them inside sub-agents, which are
  cancelled after about fifteen minutes.

## Rules

1. Find the root cause before changing anything. The cluster is a
   hypothesis that these bugs share one defect. Fix that defect once. If
   after fixing the first bug the others in the cluster still fail, STOP,
   report what you found, and do not force separate defects into one change.
2. The fix is the narrowest change that makes the wrong shape right. The
   compiler already emits correct, tight Z80 for the neighbouring shapes
   listed in each entry; the goal is to emit the same for the broken shape,
   not to route it through a slower or larger fallback schedule.
3. No existing row of `tests/perf_baselines.csv` may change, in either
   direction. Every program in `tests/` compiles correctly today, so a
   correct fix does not alter their code. If a row changes, your fix is
   wider than the defect: understand why and narrow it. Never edit a
   baseline to make the suite pass.
4. Do not edit the generators' `KNOWN_BUGS`, the reproducers, or
   `compiler-bugs.md` until the fix is proven (acceptance step 3 below).
5. Review the effect on debug metadata (`-g`, `-gline`) if your change
   moves statement or scope boundaries or register/stack locations, and run
   the debug metadata tests if so.
6. Cluster-specific:
   - dccpeep bugs (cluster K): nopeep output is the reference. The fix is
     to make the peephole rule decline the unsafe case it currently
     mis-handles, not to disable or weaken the rule for the cases it gets
     right. Run `pwsh ./scripts/run-dccpeep-tests.ps1`.
   - Runtime bugs in `DCCRTL.MAC` (BUG-039 in cluster J): use the smallest
     correct Z80 sequence and the idioms already in the file; avoid code
     bloat. Run `python3 scripts/rtl-iy-safety.py` and
     `python3 scripts/audit-runtime-coverage.py` after every runtime edit,
     and validate both the fast path and the fallback through a linked
     CP/M test.

## Acceptance — all of these, in order

1. `python3 scripts/run-known-bugs.py` reports this cluster's reproducers
   `FIXED` in both peep and nopeep, and every other reproducer still
   `fails`. If an unrelated reproducer flips, report it; do not hide it.
2. Build strict and run the regression suite:
   `DCC_MIR_REQUIRE_COMPLETE=1 DCC_MIR_REQUIRE_EMIT=1 python3 ./scripts/runall.py -mode=full -extended`
   must print SUCCESS with no perf_baselines.csv row changed.
3. Remove the cluster's features from `KNOWN_BUGS` in
   `scripts/clang_oracle_gen.py` and/or `scripts/clang_oracle_float_gen.py`
   (the entry names the feature), then run a 300-seed batch per affected
   generator and confirm it is clean:
   `python3 scripts/clang-oracle-fuzz.py --seeds 1-300 --configs production --off known --timeout 6 --jobs 6`
   (add `--gen float` for float bugs). A failure here means a facet of the
   bug is not covered: go back to step 1.
4. Promote each fixed reproducer following `tests/known-bugs/README.md`:
   move it to `tests/<name>.c` with its baseline, rewrite its header comment
   to describe the coverage, add a NEW row to `tests/perf_baselines.csv`
   measured on the normal stack-check runall path, and confirm with
   `pwsh ./scripts/validate-unit-test.ps1 -App <name>`.
5. Run `python3 -m unittest discover -s scripts/tests -p 'test_*.py'`. Do
   NOT run `scripts/prepush.ps1`; it takes five to six hours and is the
   maintainer's periodic check on `main`, not a gate for this PR. If a test
   fails, confirm it fails identically on an unchanged `main` checkout
   before reporting it as pre-existing; never refresh its expectation here.
6. Close each entry in `compiler-bugs.md` (move it under the closed section,
   update the summary table and open count), and update the Tracking table
   in `fix-plan.md`.

## Deliverable

Open a pull request against `main` that lists: the root cause in two or
three sentences; the bugs closed with `Closes #NNN` for each; the peep and
nopeep assembly for one reproducer before and after, showing the output is
as tight as the neighbouring correct shape; the fuzz batch tally; and an
explicit statement that no existing perf baseline row changed. Do not fix
bugs outside the cluster even if you see the cause — note them in the PR
instead. If you were blocked or the cluster hypothesis was wrong, say so
plainly and describe what you learned about the real grouping.
````
