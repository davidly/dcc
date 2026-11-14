# Fix plan for the logged compiler bugs

[compiler-bugs.md](compiler-bugs.md) has thirty-seven open bugs found by the
clang-oracle fuzzer (`.github/skills/clang-oracle-testing/SKILL.md`,
`docs/docs/en/appendix/06-differential-testing.md`). Many are one defect seen
from several angles. This plan groups them by *probable root cause* so that
one fix, made in the right place, closes several issues, and so that the
fuzzer can confirm it did.

The clusters are hypotheses drawn from the shapes that fail and the
neighbouring shapes that pass in each entry. A1 (#281), A2a (#283), B1 (#286),
K (#278) and L (#279) are confirmed and fixed. B3 confirms the common
forwarded-operand defect in BUG-026 and the formerly E-grouped BUG-017. The A2 hypothesis was disproved by its first
root-cause experiment and has been split into A2a and A2b below; the
B was likewise disproved and split into B1–B4. I was disproved and split
with maintainer approval into I1 (return admission) and I2 (cast signedness);
both are now fixed. Other remaining clusters are still hypotheses.
If the first fix in another cluster does not clear its siblings, stop and
report; do not force unrelated defects into one change. The maintainer
explicitly approved K as a four-rule exception.

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

### A2b. Remaining block-local expression typing (unconfirmed; after E)

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-015 | #235 | Block pointer to 32-bit object: `*q *= k` in 16 bits, `c <= *q` signed; block `unsigned char` compared as `unsigned int`; `?:` of two block floats |
| BUG-004 | #225 | `&&`/`||` assigned to a block 32-bit local leaves garbage high word (facet; the function-scope half is in E) |

Neither is fixed by A1 or A2a. Their shared nested-block condition is not
proof of a shared root cause, and BUG-015 itself has many facets. Work A2b
**after E**: if E clears the BUG-004 facet, BUG-015 is traced on its own.
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

### B2. Declaration and home bookkeeping fatals (unconfirmed)

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-014 | #234 | 32-bit local with two definitions, incremented, stored to a global: `missing direct MIR named-home offset` |
| BUG-019 | #239 | Unreachable declaration after `return`/`break`/`goto` in a nested scope: `MIR emission is required` |

Both are compile-time fatals where a declared local has no home in the view
the emitter uses. Not the B1 offset defect (unchanged by it). Trace where the
named home is looked up for BUG-014 and where an unreachable scope's
declaration is dropped for BUG-019 before assuming they share a cause; stop
and split if the first fix clears only one.

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

Standalone. The call gets the constant but the store to the local is lost.
Compare with C (merges/loop-carried values) and D (dead-store elimination)
before starting; if it turns out to share a cause with one of them, fold it
there rather than fixing independently.

### C. Assignments lost across control-flow merges and loop-carried values

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-027 | #247 | Local assigned in `if … else` nested in an `if`, then updated: lost when a loop follows |
| BUG-038 | #258 | Same with no outer `if`: lost when an early-return `if` follows |
| BUG-023 | #243 | `if (x++ > K) x = v;` leaves `x` as 1 when not taken (local assigned from another type) |
| BUG-029 | #249 | 8-bit local and another variable assigned in opposite arms: `(int)c + x` loses `x` |
| BUG-037 | #257 | Copy of a 32-bit loop counter is taken after the increment |
| BUG-052 | #272 | Folded `?:` on a 32-bit parameter in a loop accumulates into the parameter |

The first four are a value merged from two arms that is never stored before
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
| BUG-024 | #244 | `if (x++)` in a loop tests the new value (8-bit local) |
| BUG-010 | #230 | Cast or narrowing return of a 32-bit post-increment: rejected |
| BUG-041 | #261 | Value of a float `++`/`--` rejected |

Two sub-problems: the operation is done at the wrong width for 32-bit and
float lvalues (003, 044), and the *value* of the expression is wrong or
unsupported (021, 024, 010, 041). Fixing the lowering of `++`/`--` into
"load, copy old value, widen, add, store" once for all widths should cover
both. BUG-044 is the maintainer's second priority.

### E. Signedness and integer promotion

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-016 | #236 | Local takes the signedness of its initialiser, not its declaration |
| BUG-013 | #233 | Unsigned bit-field not promoted to `int` as an operand |
| BUG-006 | #226 | `/=`, `%=`, `>>=` on an unsigned bit-field sign-extend it |
| BUG-028 | #248 | `signed char >>= unsigned int count` shifts logically |
| BUG-002 | #223 | `unsigned char <<= unsigned char` not truncated on return |
| BUG-004 | #225 | 16-bit local from `&&`/`||` stored to 32-bit lvalue: garbage high word |

Each is a result typed from the wrong operand: initialiser instead of
declaration, field width instead of promoted `int`, count instead of left
operand, 16-bit logical result instead of a widened value. Start with
BUG-016 and BUG-013 (maintainer priorities) in the expression typing /
conversion insertion that feeds MIR.

### F. Compound assignment to complex lvalues

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-011 | #231 | `op=` on an element of a byte array inside a struct: rejected |
| BUG-018 | #238 | `op=` of a 32-bit value to a bit-field or to an element of a 16-bit array member: rejected |
| BUG-043 | #263 | `op=` on an integer lvalue with a float right operand: rejected |

All `DCC-E1002`. Upstream `34e35b3a` handled plain members, elements and
pointer targets; these are the remaining lvalue kinds and the mixed-type
case. Likely one lowering switch that enumerates lvalue kinds.

### G. Constant expressions in the front end

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-012 | #232 | Unary `-`/`~` on `!`, `&&`, `||` of constants: rejected |
| BUG-033 | #253 | Constant divided by constant zero in dead code: rejected |
| BUG-040 | #260 | Static float initialiser must be a bare constant |
| BUG-042 | #262 | Float constant that must be folded or truth-tested: rejected |

Constant folding that stops at the first non-trivial form. BUG-033 needs the
fold to leave an unevaluated division in place rather than reject.

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

Found while verifying B3, integer seed 255. The reduced program fails with
unchanged current-main compiler and optimizer in peep and peep-nosc, but
passes in nopeep. Independently parked, not part of B3's compiler repair;
`peepbyteself` steers only byte binary self-operands. A future fix must prove
HL dead before deleting its original load, without weakening safe cases.

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
   priority), then **B2**, **B4**.
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
| A2b | | open; after E | BUG-004/015; unconfirmed shared cause |
| B | `fix/cluster-b-stack-homes` (no PR) | stopped; split | First root fix cleared only 030; 014/019/026/036 unchanged; experiment rejected for six existing-function changes |
| B1 | [#286](https://github.com/davidly/dcc/pull/286) | merged | BUG-030 promoted as tforhome; 300 seeds clean; 500 existing perf rows and 3,366 existing census functions unchanged; strict stack/no-stack pass; approved #277 failures only (#280 fixture passes) |
| B2 | | open | BUG-014/019 bookkeeping fatals; unconfirmed shared cause |
| B3 | [#289](https://github.com/davidly/dcc/pull/289) | PR open | BUG-026/017 same root; tselfop/tandself promoted; int 213 ok/85 INVALID/2 limit-only, float 267 ok/33 INVALID; zero FAIL; 504 existing perf rows and 3,394 census functions unchanged; new BUG-054/#288 parked separately |
| B4 | | open | BUG-036; check overlap with C/D first |
| C | | open | |
| D | | open | |
| E | | open | |
| F | | open | |
| G | | open | |
| H | | open | |
| I1 | [#285](https://github.com/davidly/dcc/pull/285) | merged | BUG-046 promoted as tfltret; 300 float seeds clean; existing assembly/perf rows unchanged |
| I2 | [#287](https://github.com/davidly/dcc/pull/287) | PR open | 048/050 promoted as tflucast/tflscast; 300 float seeds clean (267 ok, 33 INVALID); existing assembly/perf unchanged |
| J | | open | |
| K | [#278](https://github.com/davidly/dcc/pull/278) | merged | Four approved independent rules; promoted tests and new performance rows; existing baselines unchanged |
| K2 | | open | BUG-054/#288 independently found during B3; live HL lost by deferred global reload |
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
