# Fix plan for the logged compiler bugs

[compiler-bugs.md](compiler-bugs.md) tracks the open bugs found by the
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
explicitly approved K as a four-rule exception, J2 as two independent
fixes with narrowly specified existing-baseline exceptions, and M as two
independent single-bug commits. J2 did not refresh BUG-039's numeric
stdout; R1 subsequently received explicit numeric/test-source/performance
baseline approval, documented in its scoped exceptions below.

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
     on the normal stack-check `runall.py` path, entries closed in
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
- Cluster pull requests are **not** gated on `python scripts/prepush.py`.
  That exhaustive proof suite (compiler mutants, sanitizers, instrumented
  coverage) takes five to six hours and is the maintainer's periodic manual
  check, run on `main` after a batch of clusters has merged. The gate for a
  cluster PR is the acceptance list above plus the regular script unit
  tests; do not run or wait for prepush. Two of those unit tests
  (`test_mir_campaign_baselines`, `test_mir_exact_controls`) fail on `main`
  today for reasons unrelated to any cluster (#277); report them as
  pre-existing, do not refresh their expectations in a cluster PR.

## Oracle seeds 70001–120000

Work locally on `fix/oracle-70001-120000`, one issue per commit. Existing
C tests, output baselines and performance rows remain unchanged; promoted
reproducers get new test names and measured rows. GitHub issues stay open
until the local commits are published.

- #236 / BUG-016: fixed locally; `tinitsgn` covers the residual word shifts
  and sign-bit-clear long constants. Both 4,142-function censuses are unchanged.
- #386 / BUG-093: fixed locally; `tbfushft` covers the unsigned view of a
  sign-extended field. Both original censuses remain identical.
- #387 / BUG-094: fixed locally; `tfzeroc` covers supported non-folding
  constant float conditions, including a runtime-guarded dead branch.
- #388 / BUG-095: fixed locally; `tfupcmp` preserves parsed float comparison
  types while update lvalues are repaired, including scoped-loop contexts.

Final fresh production campaigns with all four fixes' steers removed are
clean over seeds 120601–120900: integer 299 ok / 1 target-limit-only; float
294 ok / 6 INVALID. All seven original sources pass every production config.
All 619 old performance rows and both original 4,142-function censuses are
unchanged. Strict full+extended stack/no-stack and new tests' debug/host/
sanitizer checks pass. The user subsequently authorized removing obsolete
Python assertions. Optional steering checks now select explicit features,
and a new regression keeps the fixed features eligible with `--off known`.
The widening performance check accepts code smaller than its control while
retaining its raw-size bound and debug equivalence checks; both cases save
six bytes by eliminating two indexed reloads. All 46 focused tests pass;
full Python discovery runs 467 tests with zero failures and two skips
outside the sandbox (required for LeakSanitizer). Both pipelines pass the
four new regressions and existing `tblkword` coverage with checked
performance. This cleanup changes no compiler source, existing C test or
baseline.

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
| BUG-004 | #225 | Original block-long logical assignment fixed by A2b-1; restored byte-local logical SSA consumers remain open in A2b-3 |

The shared nested-block condition was not proof of a shared root cause.
Following the E1/E2 handoff, the first A2b fix clears **all five original
BUG-004 reproducer lines**, but not BUG-015: its parked program still has the
same nine wrong lines and byte-identical peep/nopeep assembly. Restoring
`logicinit` then exposes a separate **byte-local SSA forwarding facet** of
BUG-004 (integer seeds 133/143/153/286), so **neither complete issue is closed**.
The stop rule splits this into **A2b-1** (the fully verified block-long fix),
**A2b-2** (015's consumers) and **A2b-3** (004's newly traced byte facet).
No second compiler fix is included and no partial reproducer is promoted.
`tests/tlogblk.c` is a new, fully host-proven cleared-facet test, not a claim
that the entire expanded BUG-004 is fixed.

#### A2b-1. Canonical block assignment must type its boolean source

`mir_lower_ident_assignment_conversion()` calls
`mir_type_untyped_widened_value()` while the nested-block lvalue type is still
unknown. Later, `mir_resolve_deferred_metadata()` restores the canonical
conversion's destination to long, but never reapplies that source-typing
contract: the `&&`/`||` PHI remains untyped and conversion emission stores a
stale DE as the high word. Reuse the same helper when the named canonical
conversion is repaired. This types only an untyped PHI widened beyond a word;
constants and narrow consumers retain their existing representation.

The new block-long shape emits the exact same assembly as its already-correct
function-scope control (signed/unsigned, `&&`/`||`, stack/no-stack, release and
`-gline`). The original `block_local` keeps `spilled-scalar-cfg`; its only
addition is the five-instruction, five-byte signed-word extension already
emitted for the control: `ld a,h; rlca; sbc a,a; ld d,a; ld e,a`. No emitter,
selector, schedule, runtime, fallback or AST admission changes.

#### A2b-2. Independently traced BUG-015 consumers (split)

| Facet | Traced cause / next file and function |
| --- | --- |
| Block pointer multiplication/comparison and unsigned-word shift/divide | In `src/dcc/dcc_mir.c`, `mir_lower_expr_impl()` / `mir_lower_compound_value()` lower from an untyped indirect load. `mir_resolve_deferred_metadata()` repairs scoped binary types **before** its indirect-load pointee inference: the final load is unsigned long/word, but the consuming multiply, comparison or shift retains the earlier int computation/conversion. Pointer comparisons also retain the prematurely word-promoted signed-char/int operand. |
| Block byte beside a bit-field | `dcc_ast_build.c:p_binary()` records field-aware int promotion, but the deferred-binary branch of `dcc_mir.c:mir_resolve_deferred_metadata()` recomputes from raw `MIR_LOAD_INDIRECT.type == TYPE_UNSIGNED|TYPE_INT` without the field-width promotion. `(b & s.m5)` and its comparison become unsigned int; the negative signed-char operand becomes 65470. This borders the independently assigned BUG-013 promotion work; no bit-field edit is made here. |
| Explicit signed-word cast beside a block long | `dcc_mir.c:mir_unary_is_representation_identity()` discards the equal-width unsigned-to-signed cast because its direct consumer is a binary, not a wider conversion. The deferred binary's later width/type reconciliation observes the original unsigned load, so 0x8000 widens as +32768. Preserve the cast's interpretation across deferred/widened binary consumers, not merely its unchanged word bits. |
| Block float unary / assignment-value unary | `dcc_mir.c:mir_lower_simple_unary_chain()` lowers `-b` and `-(b = value)` with the AST's default int result; `mir_resolve_deferred_metadata()` explicitly excludes a repaired float source from ordinary unary propagation. The stored local is float but its negation is still integer. |
| Block integer/float mixed arithmetic, compound and `?:` consumers | `dcc_mir.c:mir_lower_conversion()` declines an untyped source, leaving no conversion node for the deferred repair to retype. `mir_lower_expr_impl()` and scoped repair can restore the binary or conditional PHI to float but do not materialize the missing operand conversion (e.g. integer 3 or 7 is used as IEEE bits). A host/sanitizer-proven `7 + (fg ? b : c)` probe has float arms and PHI, yet still computes 1.0f instead of 8.0f. |

The BUG-015 wrapped-word decrement facet was already corrected by E1. Its
array/member integer-to-float store examples also pass at this starting
commit (the existing deferred conversion obligations materialize those
stores); the unary, mixed arithmetic and conditional-consumer probes above
still fail and are byte-unchanged by A2b-1. No partial BUG-015 test is promoted,
no BUG-015 steering is removed, and #235 remains open.


#### A4. Direct block-pointer integer repair ordering (partial BUG-015)

Investigated from `8db72cbd` on `fix/cluster-a4-block-pointer-typing`.
The A2c early-lexical-typing experiment remains withdrawn: this change stays
inside the existing deferred metadata repair and does not bind AST locals
early or change selectors, allocation, schedules or runtime helpers.

The hypothesis is confirmed for the direct pointer facet. Named block-pointer
loads are repaired first, but their dereferences still hold the AST's assumed
int type when scoped binary repair runs. The later indirect-load pass repairs
only the load; its consumers retain signed-word arithmetic and provisional
word conversions. Infer scalar integer dereferences of repaired block-pointer
loads in the scoped fixed-point walk before repairing those consumers. Float,
aggregate and pointer pointees, bit-fields and pointer-array loads are excluded.

- BUG-015 statement lines **22/32/44/127/137**, printed lines
  **152/153/154/162/163**, now pass in peep and nopeep: long multiplication,
  signed-byte/word comparisons against unsigned long, unsigned word shift
  and unsigned word divide. The first ordering fix clears this whole facet;
  no second consumer patch is needed.
- The byte/bit-field facet (statement **55**, printed line **155**) already
  passes at the starting commit: E2-bitfield #310 made
  `mir_promoted_operand_type()` apply field-width range promotion in deferred
  repair. A4 makes no bit-field change and records that inherited correction.
- The parked program improves from **8 to 3 wrong lines of 13**. Cast lines
  **156–158** remain for A3; the separately logged float facets remain outside
  A4. **BUG-015/#235 stays open**, with its reproducer and all generator
  steering untouched. Counts stay **eight open / fifty-one fixed**.
- New host/ASan/UBSan-proven `tptrblk` covers the cleared pointer paths and
  function-scope controls, plus the already-correct byte/bit-field path.
  Its sole new normal-stack perf row measures **195016/196163 cycles** and
  **7296/7424 bytes** (peep/nopeep). Dynamic MIR probes match fully typed
  multiplication/comparison controls under release, stack-check, `-gline`
  and `-g`; linked `-gline` COM bytes are release-identical in both pipelines.
- Both censuses are byte-identical for **3,672 existing functions / 559 apps**;
  only 13 `tptrblk` functions are added. All **544 existing perf rows** are
  unchanged. Both strict full+extended suites pass (560 apps each), as do
  diagnostics, peephole fixtures, independent compiler CMake and debugger
  CTest (10/10). No existing function acquires a fallback.
- The unsigned shift uses the same `srl h`/`rr l` loop as the already-correct
  outer-pointer shape; both complete neighbouring-control assemblies remain
  unchanged. Long multiplication uses the established `__lmul` path instead
  of low-word shift/add followed by wrong sign-extension; division selects
  `__divu`, not `__divs`. Correct long arithmetic costs only the new workload,
  not any existing test.
- The required steered integer batch (seeds 1–300, production configurations,
  `--off known`) is clean: **283 ok / 14 INVALID / 3 limit-only / zero FAIL**.
  A separate diagnostic invocation temporarily restores only `blockptr`
  without editing the generator, and finds seed **96**. The reduced,
  host/sanitizer-proven remainder is a plain long store:
  `{ int32_t *q = &a3[0]; *q = x; }` stores 0 for 65536, or -1 for -65537;
  a literal 65536 also stores 0. Function-scope pointers are correct in both
  pipelines. Lowering inserted `MIR_UNARY type=2` for the assumed word target;
  later repair makes the store four bytes but cannot restore the discarded
  high word. That missing store-conversion obligation is not the load/binary
  ordering fixed here. **Stop rule applied:** stop the diagnostic batch and
  ship only the clean cleared sub-root; no second compiler patch, new bug
  number, steering removal or reproducer edit. Restored 300-seed completion
  remains unverified and BUG-015 stays open with this new documented facet.
- Python discovery ran **338 tests**, with **6 failures / 10 errors / 2 skips**:
  two failures are the accepted #277 campaign/exact controls; the others are
  process-supervision, compiler-mutation and clobber-runner harness failures.
  A focused isolated retry of the 13 affected methods leaves **1 failure /
  6 errors** (PowerShell timeouts and lifecycle/orphan-pipe assertions).
  Several timing failures clear, but the remaining harness contracts are
  unverified, not fixed or skipped. The newly added dynamic pointer test
  passes separately; debug metadata and debugger CTest pass.

#### A2b-3. Restored logical-byte initializers lose their SSA source type

The first restored integer batch reduces four failures to:

```c
int8_t b = 1 && (uint16_t)1;
a4[3] = b;                 /* uint32_t array: high word is stale */
uint8_t b = -191 && (uint32_t)0;
int32_t c = b;
g = c++;                   /* wrong high word on the zero value */
uint8_t b = (uint16_t)1 || (uint32_t)1;
s.m1 = b;                  /* uint32_t member: high word is stale */
int32_t returns_minus_one_long(void);
int8_t byte_compare = (uint16_t)1 && 29464;
g = returns_minus_one_long() >= byte_compare;  /* true instead of false */
```

`src/dcc/dcc_mir.c:mir_promote_objects()` forwards a byte local's typed load
to its untyped logical PHI. Its E1 typed-view guard requires a same-width
2/4-byte source/destination and therefore excludes this byte/word mismatch.
The already-built long conversion consequently acquires an untyped source
**after** deferred metadata repair and emits no extension. This is not the
A2b-1 canonical-assignment repair point; no second implementation is tried.

At the A2b-1 checkpoint BUG-004 parked the exact reduced seed-133 byte
facet, while the earlier word and block-long paths were promoted separately
as `tlogblk`. Only `logicbyteinit` remained steered at that checkpoint.

**Fixed in A2b-3 on `fix/cluster-a2b3-logical-byte`.** Independent MIR and
assembly inspection confirmed the forwarding trace: the final cast changed
from a typed byte load to a type-zero PHI, and neither pipeline extended DE.
The first root fix assigns the byte local's type to that PHI only when its
constant/PHI tree proves 0/1 and an existing long/float conversion needs the
source contract. No consumers, schedules, selectors or fallbacks are patched.

- All **2/2 parked lines** pass, including checksum `4001 9971d1fc`; all four
  reduced byte facets pass. Promoted `tlogbyte` preserves those two lines and
  adds fourteen lines for the other consumers, false/true variable and nested
  logical paths, assignment, short-circuit side effects and a constant control.
- Both original censuses remain byte-identical across **3,672 existing
  functions / 559 apps**. Final snapshots add only sixteen `tlogbyte`
  functions. There are no new fallback/spilled selections for existing code.
- Strict full+extended stack and no-stack gates pass: **559 apps passed,
  24 configured skips, zero failures**. All **544 existing perf rows** are
  byte-preserved, with only the measured `tlogbyte` row appended:
  **272957/292634 cycles, 7040/7424 bytes** (peep/nopeep). Its checked stack
  and no-stack runs and host validator pass.
- Restored integer production seeds **1–300** with `--off known`:
  **283 ok, 14 INVALID, 3 target-limit-only, zero FAIL**. Only the
  `logicbyteinit` open-bug mapping is removed; optional manual steering is
  retained. A 3,000-seed proof confirms the remaining known steers.
- Original reproducer Z80 adds the usual **five-byte / twenty-T-state**
  normalized signed word-to-long extension, in both pipelines, with no helper
  or frame growth. The already-correct constant-byte control and `tlogblk`
  are unchanged. Host O0/O2, ASan/UBSan and a sanitized CMake compiler pass.
- Require-emit diagnostics and both **274/274** extended MIR census modes
  pass. All **55 focused** arithmetic, line-debug, type-fingerprint, steering
  and mutation-anchor tests pass; all **10 debugger-host** checks pass.
- Before promotion: **1 FIXED, 7 fails**. After promotion: **7 reproducers,
  7 fails**; BUG-004 closes, the other seven defects remain open. Bug-log
  totals are **seven open / fifty-two fixed**. No new bugs are filed.
- The regular Python run executed **339 tests**, with **6 failures,
  9 errors and 2 existing skips**. Two failures are the pre-answered #277
  campaign-baseline/exact-control expectations; neither is changed or skipped.
  One typed-library invocation hit the canonical rebuild's temporary missing
  `dcc`; it passes after restoration. The other exceptions are timing/process
  harness checks. All thirteen non-#277 methods were retried sequentially with
  Ninja: **nine pass; four remain unverified** (one failure, five errors,
  including two teardown races):
  `CompilerMutationTests.test_child_timeout_and_environment_isolation`,
  `test_default_serial_and_parallel_are_equal_and_bounded`,
  `test_exited_worker_cleans_nested_process_scopes`, and
  `test_survivors_invalid_exits_builds_and_worker_errors_are_recorded`.
  These retain lost timeout diagnostics, 180-second scheduler timeouts,
  absent watchdog-child metadata or directory-not-empty teardown races.
  The clobber runner, inherited-pipe regression, worker watchdog and all five
  shared-supervisor checks pass their isolated retries. No harness code or
  expectations are modified; the non-green Python gate is explicitly recorded.
  No old-compiler checks, nested agents or `prepush.ps1` are run.

**D2d relationship:** this is the same broad deferred-type pipeline, **not**
one shared fix. BUG-004 loses the boolean source contract when a canonical
assignment target becomes known. D2d instead loses AST unresolved-update
provenance because `dcc_ast_build.c:ast_expr_base_ident_unresolved()` does not
descend `AST_POSTFIX` or prefix `TOK_INC`/`TOK_DEC`; dependent binary
conversions never get marked deferred. No update-provenance, unary-float or
float-admission change is included; BUG-041/056 remain parked behind
`fincdecval`.


#### A2b-1 acceptance — verified fix, both expanded issues remain open

- Original parked inventory: **19 reproducers, 1 FIXED (pre-existing 022),
  18 fails**. The first fix makes original 004 FIXED (all five lines), with
  015 still nine wrong lines and identical assembly. After parking the newly
  reduced byte facet, final inventory returns to **1 FIXED, 18 fails**;
  there are still **nineteen open, thirty-five fixed issues**. No issue closes.
- Restored integer discovery, all four production modes, seeds 1-300:
  **292 ok, 1 INVALID, 3 target-limit-only, 4 FAIL** (133/143/153/286, each
  independently reduced and traced to the remaining byte PHI). With only
  `logicbyteinit` steered, final integer batch: **296 ok, 1 INVALID,
  3 target-limit-only, zero FAIL**. Float steering unchanged: **282 ok,
  18 INVALID, zero FAIL**. Both final 3,000-seed steering proofs pass.
- Both original **3,545-function** census files are byte-identical after the
  compiler fix. Final snapshots add only ten `tlogblk` functions; filtering
  that new app leaves every existing row/hash byte-identical in both stack
  modes. All **526 existing perf rows**, including their order and exact
  bytes, are untouched; the one new normal-stack row is
  `tlogblk,231255,232566,6400,6656` (peep/nopeep cycles, peep/nopeep sizes).
- New `tlogblk`: original five lines plus six false/true/nested block logical
  lines; host clang O0/O2 and ASan/UBSan agree. Host validation and focused
  strict full run pass. Integer widened bodies match the resolved
  function-scope controls exactly, before and after dccpeep. The original
  peep reproducer adds only five machine bytes / 20 cycles; its quantized COM
  crosses 2304 to 2432 bytes. No selector, schedule, runtime or fallback change.
- Strict Python full+extended: **541 passed, 24 skipped**, zero failures before
  adding the new app. Final strict PowerShell stack and no-stack full+extended:
  **542 passed, 24 skipped**, zero failures; diagnostics, dccpeep, extended and
  checked stack performance gates pass. Canonical and independent CMake
  compiler builds, require-emit diagnostics and both **274/274** extended
  MIR censuses pass.
- Debug review: only an internal boolean PHI becomes int; declaration types,
  statement/scope boundaries and existing homes/frames are unchanged. Full
  debug records block `b` as a four-byte long at its actual stack offset.
  Existing typing/entrypoint/line-debug tests **51/51**, debugger-host CTest
  **10/10**, and final new typing/generator cases **11/11** pass. The expanded
  eleven-line app passes all twelve release/full-debug/line-debug pipeline
  combinations; every `-gline` COM is release-identical. Generic block-float
  logical conversions are additionally host/sanitizer-proven in the same
  twelve modes, with correct `__fif` conversion and release-identical `-gline`.
  A floated cross-scope exact-assembly experiment retained extra initializer
  constant loads in the block form, so no float cross-scope identity is
  claimed; the permanent float test checks the actual conversion and line-debug
  contracts instead. No initializer-layout optimization is attempted.
- Full regular Python discovery: **326 tests, six failures, four errors,
  two existing skips**. Two failures are the approved #277 campaign/exact
  controls; all eight other timing/harness cases were retried individually.
  Child environment/timeout, inherited-pipe cleanup and overall deadline pass
  their retries. Five remain **unverified**: mock default serial/parallel and
  survivor runs hit 180 seconds (the latter also has a cleanup-directory error),
  worker-watchdog and exited-worker retries lack the `watchdog-child` marker,
  and post-exit grace takes 3.118 seconds against its three-second threshold.
  These unchanged synthetic harnesses are not the modified compiler. No harness
  fix, expectation refresh, skip, old-compiler check or prepush run was made.


### B. Disproved: split into B1–B4

The original B group (BUG-014, 019, 026, 030, 036) was attempted on
`fix/cluster-b-stack-homes` from main `38952e7a`. Repairing BUG-030 cleared
only that reproducer; the other four failed identically and no outside-cluster
reproducer flipped. The grouping was wrong, and the experiment itself was
rejected because it changed six existing functions. The maintainer approved
the split below.

### A2c. Stopped: early block typing changes existing generated output

| Bug | Issue | Outcome |
| --- | --- | --- |
| BUG-015 | #235 | Root experiment passes the parked integer program and all additional float/mixed probes, but violates existing-output and performance gates; withdrawn |
| BUG-004 byte facet | #225 | Does not fall out of early block typing: the parked function-scope byte PHI still fails; no SSA forwarding patch attempted |

Investigated on `fix/cluster-a2c-block-type-resolution` from `15db8e69`.
**No compiler change is retained, no issue closes, no reproducer is promoted,
and no steering is removed.** The log stays at **twelve open / forty-three
fixed**. This is an architectural stop, not another subdivision into consumer
patches. The successful trial results below are experiments, not accepted fixes.

#### Why the declared type is missing at lowering

1. **The sizing scans know types but discard lexical bindings.**
   `dcc_func.c:3462-3518` runs `scan_function_body()` twice under
   `asm_suppress_depth`, then resets `g_frame.nlocals` to the parameters and
   reserved temporaries for production processing. In `dcc_symbols.c`,
   `add_local_known()` stamps the declared type immediately (574-590), while
   `leave_scope()` truncates the active local table and pops lexical renames
   (428-449); `find_local()` only searches that active range (461-494).
   Suppression is not the type-erasure operation: restoring/truncating the
   parser's symbol state is. Sizing is not a retained semantic symbol table.
2. **Nested declarations are token spans, not parsed declarations in the
   expression-building walk.** `dcc_ast_build.c:1354-1400,1536-1567` captures
   `AST_DECL` and skips the complete declaration tokens, without entering a
   lexical scope or declaring their objects. `p_primary()` (652-658) records
   `find_sym()` in `AST_IDENT.sym`; a newly introduced block local therefore
   has a null binding and zero identifier type. `ast_expr_type_for_sizeof()`
   (166-177) falls back to int, and `p_unary()/p_binary()/p_conditional()` bake
   that assumption into result/operand types and deferred flags. The enclosing
   function's declarations instead run through `dcc_stmt.c:process_compound()`
   before subsequent statements are built, so their types are available.
3. **The support probe creates the symbol only temporarily.**
   `dcc_ast_stmt_classify.c:1171-1246,1282-1368` replays block/for-init
   declarations in `scan_mode`, validates the dependent expressions with their
   symbols visible, then restores the local/frame/rename state. It does not
   bind/retype those previously built expression nodes. `dcc_decl.c:1590-1656`
   assigns the real declared type and publishes `mir_note_declared_symbol()`
   even during an active-MIR support probe; however that name-indexed table is
   not an immutable, lexically bound `Sym` for each AST use. Probe frame offsets
   and internal block names can also differ from real replay (B1).
4. **MIR and metadata are separate ordered walks of each accepted subtree,
   not one interleaved declaration/use walk.**
   `dcc_ast_stmt_meta.c:514-515` calls `mir_capture_stmt(n)` for the entire
   subtree **before** `ast_process_stmt_metadata(n)`. `dcc_mir.c:4330-4347`
   lowers declarations to `MIR_DECL_PLACEHOLDER` and lowers their later uses
   immediately. `mir_ident_symbol()` (1177-1190) consults active symbols or the
   AST binding, not the probe's declared-type table; consequently named loads
   can be untyped and operations/conversions have already been chosen.
   `mir_lower_conversion()` (1758-1777) omits a conversion from an untyped
   source. Only the subsequent metadata walk enters scopes and calls
   `ast_replay_decl_span()` (`dcc_ast_metadata.c:132-140,293-326`), splices
   initializer MIR into the placeholders, and records scope-qualified aliases.
   `mir_end_function()` then invokes `mir_resolve_deferred_metadata()`
   (`dcc_mir_select.c:4171`). That repairs names/types, but cannot generally
   recreate conversions or the declared interpretation already discarded.

Thus the unresolved-block condition discussed across A1/A2a/A2b/E1/B2b
is real: the semantic declaration binding is unavailable to the original
expression/MIR walk. This does not make every later SSA defect the same fix. It is **not** a declaration whose type parser has yet to decide its
width, nor a Z80 emitter choosing a type despite a fully typed input.

#### Root experiment and the hard-gate failure

The small architectural trial reused the existing support replay while its
lexical symbols were live: arena-copy each originally unbound scoped local
`Sym`, and recompute dependent expression/operand/conditional types before
MIR capture. It did not edit `dcc_mir.c`, selectors, emitters, exact schedules,
runtime, SSA promotion or the D2e unresolved-update provenance helper. Both
frame-sizing scans and real declaration replay stayed in place. The intent was
to present typed local reads to the normal lowering rules rather than add any
new `mir_resolve_deferred_metadata()` repair points. The trial patch and all
logs/probes are under `build/a2c/`, uncommitted.

- Both starting censuses cover **3,613 functions / 550 apps**. In **each**
  stack mode the trial changes **259 functions across 63 apps**, with **40
  selector changes**, **109 functions with changed selection/size/instruction
  metrics**, and **150 additional hash-only changes**. Every changed function
  has a different selected hash. Thirty-six exact `scheduled-machine-cfg`
  selections become `spilled-scalar-cfg`, one becomes `hybrid-homed-scalar-cfg`;
  three former spilled functions choose homed/hybrid paths. Coverage remains
  3,613/3,613, so `--fail-on-regression` alone is insufficient: it checks MIR
  coverage, not the required exact equality of every census field.
- New known types materialize conversions **before** MIR numbering,
  promotion and allocation. Existing correct bodies that used late
  selection-time conversion now have different value lifetimes, spills and
  frame sizes. This is substantive machine output, not merely debug records
  or label renumbering. Selected `generated_bytes` measures **assembly text**,
  not machine bytes; its aggregate increase is 147,906 without stack check
  and 147,892 with it. Actual linked sizes/cycles below are the performance
  evidence. Hash-only rows are not claimed to be all semantically different.
- Four existing apps' stdout still passes both pipelines, but their checked
  performance does not. No baseline was refreshed:

  | Existing app | Peep cycles: baseline → trial | Nopeep cycles: baseline → trial | COM bytes: baseline → trial (peep / nopeep) |
  | --- | --- | --- | --- |
  | `tforhome` | 482,496 → 484,048 | 532,236 → 536,180 | unchanged |
  | `tblktern` | 55,516 → 56,917 | 56,751 → 58,074 | 8,576 → 8,704 / 8,704 → 8,832 |
  | `catalan` | 516,129,562 → 516,485,661 | 526,380,230 → 526,751,318 | 8,448 → 9,216 / 8,576 → 9,344 |
  | `tblktype` | 29,911 → 29,901 | 30,988 → 30,951 | unchanged |

  Even the `tblktype` improvement is prohibited by the existing-row rule.
- Concrete unchanged-correct function comparisons explain why:
  `tblktype.long_add` removes a two-byte temporary and performs its word-to-long
  extension earlier; `tblktype.long_gt_var` changes a two-byte narrow temporary
  into a four-byte wide temporary, extending before its store rather than
  after its reload; `tblktern.sub_member` goes from register/stack argument
  staging with no local spill frame to an eight-byte spill frame and extra
  store/reload pairs; `tblktern.div_logical` grows its frame from eight to
  twelve bytes. `tforhome.array_in_else` changes its byte decrement from
  `or a; sbc hl,de` to the typed byte-unit-update sequence. `catalan.main`,
  `tforsco.main`, and `tmodp2.main` lose exact schedules because the typed
  instruction/conversion/object shape no longer matches the incumbent proof.
  No application-name production gate or general-emitter fallback was added.

A production root fix would need **immutable lexical declaration identities
and typed expression construction before lowering**, independent of sizing
and initializer capture. Retaining `locals[]` across scopes is not safe:
shadowed names, siblings, for-init renames, arrays/VLAs and reused local slots
require distinct bindings. Reordering the metadata walk ahead of capture is
not safe either: initializer/scope replay matches already-created MIR
placeholders and can retain scalar/VLA probe instructions. Simply copying
probe `Sym`s must not make their provisional offsets authoritative (B1).
Achieving identical existing output would additionally require a deliberate
MIR conversion/lifetime canonicalization and exact-schedule migration that
reproduces today's repaired representation. That is wider than a type-binding
fix under these hard gates. **Stop rather than patch the five consumers or
expand the SSA byte guard.** This does not prove every future root design is
impossible; it proves the investigated early-typing path is not output-preserving.

#### Facet probes and debug review

- BUG-015's parked 13-line integer program passes peep and nopeep under the
  trial. At this starting commit it has **eight**, not the historical A2b
  table's nine, wrong lines. No acceptance claim is based on that older tally.
- A separate **26-line** probe agrees between host clang O0/O2 and ASan/UBSan
  and passes both trial pipelines. It covers float unary `-b`, `-(-b)`,
  `-(b = value)`; signed word/long and unsigned-byte locals in float arithmetic;
  global float compound add/multiply with a block integer; mixed float/integer
  add/subtract/multiply and compound subtraction; assignment-value arithmetic;
  both branches of `7 + (fg ? b : c)` with two block floats; a for-init integer
  consumer; array/member/pointer float stores; and function-scope controls.
  Along with the parked pointer multiplication/comparison, unsigned-word
  shift/division, byte/bit-field and explicit signed-word-cast tests, every
  A2b-2 table row is probed. This is not a restored-fuzz closure proof.
- BUG-004 remains wrong in both trial pipelines (one checksum line out of
  two). Its remaining parked byte initializer is **function-scope**, already
  typed at lowering; `mir_promote_objects()` subsequently replaces the typed
  byte load with an untyped logical PHI. Early block binding cannot repair
  that independent post-lowering loss. No E1 guard or C2 liveness path is edited.
- Full `-g` declaration/end records are identical before/under the trial:
  **58** records in the mixed probe and **78** in `tforhome`, including each
  block float's four-byte declared type/offset and balanced variable lifetime
  records. Real metadata replay still owns them. This is necessary but not
  sufficient: the observed release spill-frame changes mean debugger location
  mapping must be revalidated against the selected machine layout in any
  production redesign. The mixed probe's `-gline` COM is **not** release-identical
  in either pipeline, both with the trial and with the restored, unchanged
  compiler: three redundant `exx; exx` pairs remain around the inline reporting
  calls after array/member/pointer loads. This matches the already documented
  metadata-separated cleanup observation in E2-bitfield and D2d, rather than
  a newly introduced type-binding defect. No cleanup is patched or new issue
  manufactured. Runtime output agrees between release/line-debug within each
  compiler state; release-identical line-debug and the full debugger-host
  contract for the rejected new shapes remain unverified.

#### Final retained state and validation

The compiler trial is fully withdrawn and the canonical toolchain rebuilt.
Both final census files are **byte-identical** to the starting files, all
**535 existing performance rows** are byte-identical, and all bug-log and
steering files remain untouched. Final `run-known-bugs.py` is **12 reproducers:
1 FIXED (the pre-existing BUG-022 fixture), 11 fails**; BUG-004 remains one
wrong line out of two and BUG-015 eight out of thirteen. BUG-022 belongs to M
and is not changed. Final strict focused full-mode `tblktype,tblktern,tforhome,
catalan` is **4 passed / zero failures**, with all checked performance passing.
The extra mixed probe returns to its original **5/26 passing lines** in both
pipelines, confirming that no experimental float fixes leak into the retained
compiler. No old-compiler/regression-dating check or
`prepush.ps1` was run. Restored 300-seed batches, promotions, full+extended
release suites and full Python/MI gates are not run for this **documentation-only
stopped PR**: no feature has been accepted/restored. The approved #277 campaign
and exact-control unit failures remain outside scope; no harness expectation,
timeout or skip is changed. No new campaign finding or issue is filed.

Exact trial changed-function inventory follows. An asterisk means **hash-only**
(same selector, assembly-text size, instruction and block counts); unmarked
functions change at least one of those metrics. Both stack modes have the same
259-function set. Hash-only rows are retained here because exact census
identity, rather than only runtime behavior, is the requested gate.

| Existing app | Changed functions |
| --- | --- |
| `adaint` | `var_or_const_decl` |
| `bint` | `acc`, `add_patch`*, `assignment_name`*, `compile_line`*, `compile_prog`*, `dim_stmt`*, `expr`*, `factor`*, `find_line`*, `for_stmt`*, `goto_line_op`*, `if_stmt`*, `isvarname`*, `load_file`*, `main`*, `need`, `next`, `next_stmt`*, `print_stats`*, `print_stmt`*, `relation`*, `resolve_patches`*, `run`, `split_lines`, `statement`*, `sum`*, `sym_find`*, `term`* |
| `calc` | `parse_unary`* |
| `calcopt` | `parse_unary`* |
| `catalan` | `main` |
| `cint` | `global_decl_or_func`, `init_state`*, `load_file`*, `main`*, `preprocess`, `primary` |
| `cobint` | `add_para`*, `add_stmt`*, `check_idx`*, `check_idx_get`*, `check_idx_set`*, `compile_add`*, `compile_compute`*, `compile_condition`*, `compile_display`*, `compile_divide`*, `compile_expr`*, `compile_factor`*, `compile_goto`*, `compile_if`*, `compile_move`*, `compile_multiply`*, `compile_perform`*, `compile_primary`*, `compile_rel_one`*, `compile_stmt_seq`*, `compile_store_after_value`*, `compile_subtract`*, `compile_term`*, `compile_var_load`*, `decode_stmts`*, `emit`*, `exec_range`, `get_occurs`*, `get_pic_esize`*, `load_file`*, `main`*, `parse_data_line`*, `parse_source`*, `print_stats`*, `run_bc`*, `tokenize_stmt` |
| `fint` | `add_string`*, `compile_definition`*, `compile_word`*, `ctl_pop`*, `ctl_push`*, `exec_word`*, `init_state`*, `load_file`*, `main`*, `need_name`*, `next`, `prim`*, `print_stats`*, `run_at`, `top_level`* |
| `forint` | `primary`* |
| `ln2` | `main` |
| `nqueens` | `isSafe`, `main`*, `match`, `solve`* |
| `pint` | `calc_code_limit`, `init_run_storage`*, `load_file`, `main`, `print_stats`, `run`, `xcalloc` |
| `tarray` | `ShowBinaryData`, `main`, `test_many` |
| `tbdos` | `main` |
| `tbios` | `main` |
| `tblkdiv` | `by_three`, `member`, `member_of_element`, `remainder_of` |
| `tblktern` | `div_logical`, `div_mul`*, `ge_unsigned`, `mod_sub`, `mul_narrow`*, `shift_loop`, `sub_member`, `tern_logical` |
| `tblktype` | `long_add`, `long_gt_var` |
| `tc99scpe` | `pointer_for_init_sizeof` |
| `tchess` | `gen_pseudo` |
| `tcmpabi` | `insertion_sort`, `main`* |
| `tcmpval` | `elem_sub_block`, `member_mod_block` |
| `tdeadblk` | `after_break`, `decl_before_label`, `for_init_after_return`, `label_reentry`, `vla_effect` |
| `terrno` | `main` |
| `texecdp` | `main`*, `recurse` |
| `tfopenw` | `main` |
| `tforblk` | `main` |
| `tforhome` | `address_taken`, `array_in_else`, `no_earlier_declaration`, `pointer_walk`, `sibling_blocks`, `switch_case` |
| `tforsco` | `main` |
| `tfpraw` | `main` |
| `tfreopen` | `main` |
| `tgnarly` | `main` |
| `tinittyp` | `facets` |
| `tlngnarw` | `main` |
| `tlongaud` | `main` |
| `tm` | `main` |
| `tm1mu` | `main` |
| `tmirconv` | `check_block_scoped_arguments`*, `check_float_to_long_arguments`*, `check_narrow_index_wide_bound`*, `copy_with_long_count`*, `count_bound_on_left`, `count_down_to`*, `float_block_to_long`*, `float_in_loop`*, `float_loop_to_long`*, `float_to_byte_argument`*, `float_to_word_argument`*, `main`*, `narrow_long_argument`*, `narrow_long_in_loop`*, `narrow_long_to_byte`*, `splice_at_long_offset`*, `word_to_float_argument`*, `word_to_float_in_loop`*, `word_to_long_argument` |
| `tmodp2` | `main` |
| `tnestfor` | `main` |
| `too` | `main`*, `make_piece`*, `test_aggregates`*, `test_gallery`*, `test_multidim`, `test_scope`, `test_size_inference`*, `test_size_inference2`* |
| `tpeepi` | `count_open`, `highest_open` |
| `tphi` | `main` |
| `tpihexb` | `main`*, `parse_block_arg`, `visit_indices` |
| `tppasgn` | `advance` |
| `tprintf` | `main` |
| `tpromo2` | `main` |
| `tpromo32` | `main` |
| `tptrarr` | `main`*, `step` |
| `tqsort` | `t_qsort_struct` |
| `tregwide` | `count_le` |
| `tselfop` | `block_assigned` |
| `tsibdecl` | `main` |
| `tstchain` | `main` |
| `tstr` | `main`, `test_wide` |
| `tstring` | `main` |
| `tsvbuf2` | `main` |
| `tsyntax` | `main`*, `test_additive_subscripts` |
| `ttrig` | `main` |
| `tvla` | `fixed_cast_bounds`*, `fixed_sizeof_bounds`*, `main`*, `vla_3d`*, `vla_back_exit_inner`*, `vla_big_frame`*, `vla_call_bound`*, `vla_cond_sibling`*, `vla_dependent_bound`*, `vla_fixed_after`*, `vla_forinit_dep`, `vla_fwd_exit_all`, `vla_fwd_exit_inner`*, `vla_fwd_in_loop`*, `vla_fwd_same`*, `vla_goto_out`*, `vla_in_loop`, `vla_long_bound`*, `vla_long_rhs_store`*, `vla_longjmp`, `vla_memset_sizeof`*, `vla_nested`, `vla_pass2d`*, `vla_ptr10d_deref_chain`*, `vla_ptr2d_deref_chain`*, `vla_ptr2d_deref_chain_contexts`*, `vla_ptr2d_deref_chain_switch`*, `vla_ptr2d_deref_chain_while`*, `vla_ptr3d_deref_chain`*, `vla_ptr3d_deref_chain_do`*, `vla_ptr_diff`*, `vla_ptr_elem`*, `vla_return_nested`, `vla_side_bound`*, `vla_sizeof_if_body`*, `vla_sizeof_loop_changes`*, `vla_sizeof_op_cmp`*, `vla_sizeof_subscript`*, `vla_sizeof_ternary`*, `vla_subscript_bound`, `vla_sum2d`*, `vla_switch` |
| `tvlaisle` | `count_islands` |
| `tvlaparm` | `cgrid_sum`*, `grid_fill`*, `grid_sum`*, `main`*, `smooth` |
| `tvlax` | `checksum_loop`*, `goto_stable`, `loop_stable`, `main`*, `nested_stable`, `rec_probe`* |

### A3. Deferred widened consumers must observe word interpretation

| Bug | Issue | Outcome |
| --- | --- | --- |
| BUG-058 | #315 | Fixed; promoted as `tblkword` |
| BUG-015 signed-word cast facet | #235 | Output statements 156–158 fixed; host-proven `tblkcast`; full issue/reproducer/steering retained |

Investigated independently on `fix/cluster-a3-block-word-widening` from
`8db72cbd`. The shared loss is confirmed, not the rejected A2c early-typing
experiment: both paths already have the correct declared/cast word type,
but representation reuse discards it before implicit widening.

- BUG-058: deferred repair restores the block array-store load to signed
  word, but `mir_promote_objects()` forwards it to the unsigned initializer.
  Its typed-view guard only sees explicit widening unary consumers (hence
  the correct scalar-global store), not the implicit wide indirect store.
- BUG-015: `mir_unary_is_representation_identity()` removes the unsigned-to-
  signed word cast whose direct consumer is a deferred binary. The binary's
  late widening then sees the original unsigned load and extends 0x8000 to
  +32768. Function-scope controls already have explicit widening conversions.

The existing guards now share `mir_use_widens_value()`: recognize implicit
wide indirect stores and wider deferred binary data operands, retaining
exactly the needed typed word views. A shift count is not a widened data
operand. No parser/early typing, source boundary, selector, machine schedule,
runtime, fallback, byte-PHI, pointer, bit-field or float-admission edit is made.

All nine original BUG-058 lines pass in both pipelines. BUG-015 changes from
8/13 to 5/13 wrong lines: only output statements **156–158** flip (cast compare,
cast shift compare, cast sum; expressions 66, 77, 88); lines **152–154 and
162–163** remain wrong.
The parked `bug015.c` is unchanged, #235 stays open, and no BUG-015 steer is
removed. `tblkcast` covers the cleared facet plus mirror signedness, reversed
operand order, same-width and sign-boundary controls; clang O0/O2 and
ASan/UBSan agree on all eleven lines.

Both original **3,672-function** census TSVs are byte-identical, including
every selector and selected hash; final snapshots add only 18 new functions
(3,690 total). No existing fallback is added. All **544
existing performance rows** remain byte-identical in their original order.
New stack-checked rows only: `tblkcast,226125,226907,5888,6016` and
`tblkword,405538,406521,6400,6528`. Original `block_init` is **56→58 machine
bytes peep / 63→77 nopeep**. Its peep body is identical to the correct
same-signed control. Nopeep retains a 12-byte typed-word home store/reload
beyond that control, removed by existing dccpeep rules; no runtime extension
helper is added. Every original correct fixture function is unchanged.

`blockwordinit` is removed only from `KNOWN_BUGS`, restoring production
integer seeds 1–300: **290 ok, 7 INVALID, 3 target-limit-only, zero FAIL**.
All four production configurations are covered; 3,000 seeds retain no
remaining steered shape. No new compiler bug is found. Final parked inventory
is **7 reproducers / 7 fails**; log tally **seven open / fifty-two fixed**.

Canonical and independent CMake compiler builds pass. Compiler ASan/UBSan
and cache-verify probes, required-MIR diagnostics, both **274/274** extended
MIR censuses, debugger-host CTest **10/10**, existing focused typing/entrypoint/
line-debug Python tests **51/51**, and new tightness/generator tests **9/9**
pass. The two repaired shapes pass all **24** release/full-debug/line-debug,
stack/no-stack, peep/nopeep pipelines; all eight `-gline` COMs are release-
identical. Full debug retains the signed block word declaration at its actual
two-byte frame home. Existing symbols, scopes, statement boundaries and
locations are unchanged because existing function output is unchanged.

Initial strict native Python full+extended regression is **559 passed,
24 skipped, zero failures**. Final strict PowerShell full+extended runs in
both stack modes are **561 passed, 24 skipped, zero failures**; stack-checked
performance, diagnostics, dccpeep and extended gates pass. Final censuses have
only the 18 new test functions; filtering those rows is byte-identical to
both starting files. New normal host validations and the repeated promoted
source debug matrix/tightness/generator checks all pass.

Regular Python discovery: **338 tests, six failures, ten errors, two existing
skips**. The campaign-baseline and exact-control failures are the approved
[#277](https://github.com/davidly/dcc/issues/277) exceptions. Every other
failing test was retried individually: **9/13 pass**, including inherited-pipe
cleanup, healthy-baseline inclusion, watchdog, environment/explicit policy,
post-exit grace, clobber-runner orphan supervision, zero completion budget,
pipe-drain budget and deadline/explicit-stop cleanup. Four unchanged synthetic
mutation-runner cases remain **unverified**, not skipped or fixed:

| `test_mir_compiler_mutations` case | Isolated retry |
| --- | --- |
| `test_child_timeout_and_environment_isolation` | outer harness command times out after about 20 seconds |
| `test_default_serial_and_parallel_are_equal_and_bounded` | mock runner command times out at 180 seconds |
| `test_exited_worker_cleans_nested_process_scopes` | missing `watchdog-child` marker after 7.653 seconds |
| `test_survivors_invalid_exits_builds_and_worker_errors_are_recorded` | mock runner command times out at 180 seconds; teardown also reports a nonempty fixture directory |


No old-compiler check, prepush, expectation refresh, harness fix or new skip
is made. Concurrent A2b-3/A4/D2e-2/S1/F1 compiler surfaces are untouched.

**A3 close-out (BUG-015 promotion):** after #328 (A4) merged and the D2e-2
root repair `f311a376` (`mir_repair_deferred_float_types()`, `tflbconv`) was
cherry-picked onto this branch, `run-known-bugs.py bug015` reported `FIXED`
in both modes. The reproducer is promoted as `tblkbind`
(`tblkbind,214401,215283,5120,5376`; `tflbconv` remeasured under the F1
runtime as `129599,130323,5760,5888`). The `blockptr`, `blockcast`,
`blockdec`, `fblockneg` and `fblockint` steers are removed. Restored integer
seeds 1–300: 293 ok / 3 INVALID / 3 limit-only / 1 FAIL (seed 261, parked as
BUG-064/#333); restored float seeds 1–300: 295 ok / 5 INVALID / 0 FAIL. Seed 96
(A4's store-conversion remainder) is parked as BUG-063/#332 with a new
`blockstore` integer steer. Census against main `73d9daa4`: 3,736 functions
on both sides, zero existing-function changes; the 14 differing functions are
all in `tblkbind`/`tblkcast`/`tblkword`/`tflbconv`. The agent stopped on
quota before documentation; the maintainer's orchestrator finished the branch
directly. D2e-2's float update admission scope (BUG-041/056/059) is not part
of this close-out and stays open.

### D2e-2 (admission). Float `++`/`--` value admitted

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-041 | #261 | `g = f++`, `if (f-- > x)`, `while (f-- > 0)` rejected with DCC-E1002 |
| BUG-056 | #299 | `-(s[1].a[1]++)` negated as an integer (latent, admission-blocked) |
| BUG-059 | #316 | `-(local++) + (uint16_t)1` lacks the int→float operand conversion (latent) |

**Confirmed root cause (admission half):** `ast_value_is_float_word()` asked
`ast_expr_supported()` before looking at the update, and neither the
scalar-binary nor the other-expression classifier had a float case for
`TOK_INC`/`TOK_DEC`, so the value of any float update was rejected. The two
latent wrong-code facets (056, 059) were downstream typing defects that only
an admitted update could reach; their roots were repaired by D1 and the
D2e-2 root repair (`mir_repair_deferred_float_types()`, shipped via #331).

**Fix:** `ast_update_lvalue_float_type()` proves the updated lvalue is a
float scalar, array element, member or dereference; the scalar-binary
pre-update case, the other-expression postfix case and
`ast_value_is_float_word()` (checked before expression admission to avoid
the recursion) admit it. `fincdecval` leaves the float generator's
`KNOWN_BUGS` (now empty).

**Debug-metadata fix found by the admission gate:** the admitted BUG-056
shape selected the compacted regional homed CFG, whose text-adjacency
rewrites (`mir_compact_regional_candidate`, `mir_compact_adjacent_exx`) did
not see across interleaved `;@dcc-loc`/`;@dcc-line` lines, so `-gline` kept
`exx; exx` pairs that release elided (D2d had noted this). Both passes now
match instruction lines only and replay skipped marker lines ahead of the
rewritten span. Release output has no body comments, so release text is
unchanged.

**Validation:** bug041/056/059 FIXED in both modes; all 594 test outputs
byte-identical against main `464af0b6` (only `tstdc`'s `__TIME__`); all 546
existing perf rows unchanged; strict runall full+extended SUCCESS; Python
tool tests OK; `-gline` binaries identical to release for all three
reproducers in peep and nopeep; strict `-g` and `-gline` census 3753/3753;
`test_line_debug` and debugger-host ctest (10/10) pass; integer seeds 1–300:
296 ok / 4 INVALID / 0 FAIL; float seeds 1–300 with `fincdecval` restored:
297 ok / 3 INVALID / 0 FAIL. Promoted as `tfincval`, `tfnegupd`, `tfupdcnv`, plus the
block-integer-operand companion `tblfcons`.

### T2. Implicit store conversion fixed to the assumed pointee

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-063 | #332 | `{ int32_t *q = &a[0]; *q = x; }` stores only the low word |

**Confirmed root cause:** the `AST_INDEX`/`*` assignment path calls
`mir_lower_conversion(value, target_type)` with the target type known at
lowering. A block-scoped pointer is not yet typed, so the target is the
assumed `int` and a typed `int32_t` source gets an eager narrowing
`MIR_UNARY`. The indirect-store repair later sets the store's type and
`memory_size` from the real pointee but never revisited that conversion.
`deferred_conversion_type` does not cover it: it only handles untyped
(type 0) sources.

**Fix:** the implicit conversion is flagged (2048) at lowering; when the
repair changes a `MIR_STORE_INDIRECT`'s type, `mir_retarget_store_conversion`
bypasses the conversion if the source already has the pointee type and
re-types it otherwise. The flag is cleared after repair because spilled
matchers require unflagged unaries. Explicit casts are not flagged.

**Validation:** `bug063` FIXED in both modes; all existing test outputs
byte-identical against main `96cd2f05` (only `tstdc`'s `__TIME__`); all 546
existing perf rows unchanged; strict runall full+extended SUCCESS; Python
tool tests OK; `blockstore` steer removed, integer seeds 1–300: 296 ok /
4 INVALID / 0 FAIL. Promoted as `tblkstor`
(`tblkstor,102746,102844,3328,3328`).

### T4. Multi-dimensional array member `sizeof`

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-069 | #341 | `sizeof t.c` for `int16_t c[2][2]` reports 4; `sizeof t.c[1]` reports 2 |

**Confirmed root cause:** `ast_sizeof_expr_value()` sized an `AST_MEMBER`
array field as `elem_size * array_len`, and `FieldDef.array_len` is only the
first dimension; its `AST_INDEX` case handled identifier roots only, so a
partially indexed member fell through to `type_size()` of the scalar element.

**Fix:** the member case returns `fd->size`; the index case gains a member
root branch that, for `index_count < fd->dim_count`, returns `fd->elem_size *
field_array_elems_from_level(fd, index_count)` (now exported from
`dcc_decl.c`, mirroring the identifier path's
`sym_array_elems_from_level`). No MIR change, so no exact-schedule hashes
moved.

**Validation:** both strict runall full+extended suites SUCCESS; 0 perf
regressions / 146 improvements against main across 561 apps; dccpeep
fixtures 73/73; debugger ctest 10/10; ASan+UBSan dcc clean over tszmdim,
tszderiv, tptrcasg, tsizesgn, tszblock, tstruct; `-gline` output identical
and `-g` run correct; Python tool tests pass except the pre-existing
cmp15ok control; integer seeds 1–300 `--off known`: 295 ok / 5 INVALID /
0 FAIL. Promoted as `tszmdim` (`tszmdim,150822,150992,3456,3456`), which
also covers three-dimensional members, `sizeof a / sizeof a[0]` idioms,
members through pointers and nested-struct arrays, and struct sizes that
agree with the padded host layout.

### T5. Proof-suite gates: compound-check exact control and process supervisor

| Issue | Shape |
| --- | --- |
| #277 | `test_mir_exact_controls` cmp15ok/cmp4ok/cmp45ok: the compound-check-runner exact schedule rejected `main` in `cmpw4.c` since `523f5738` (the bool-branch rewrite) |
| #280 | `test_mir_compiler_mutations` intermittently reports exit 139 with an empty log in whichever phase it wrapped |

**#277:** the rewrite inserts three identity `int` conversions into `main`'s
MIR (976 instructions, NOP 139, UNARY 22; final load at 966) while the
semantic event walk the schedule proves is unchanged (71 checks, helper at
58, 188 events). The fingerprint gate in
`mir_match_compound_check_runner()` was refreshed to the new shape, and
`scripts/compound-wave45-audit.py` now records the exact schedule's baseline
hash (`40d373f3…`, S2 had captured the generic emitter's) with the post-522
mutation indices shifted by two. Audit: 13/13
mutations rejected, 24 source controls pass; cmpw4 peep 9344 → 8448 bytes,
2,739,369 → 2,733,407 cycles; nopeep 9856 → 9344 bytes, 2,893,002 →
2,886,530 cycles, identical output.

**#280:** not a compiler bug. macOS has no `setsid` utility, so
`Start-SupervisedProcess` launched a fresh `pwsh` supervisor (with an
`Add-Type` C# compile) for every supervised process; a mutation run spawns
hundreds, and a known intermittent .NET arm64 startup SIGSEGV surfaces as
exit 139 with no log. The module now prefers `setsid`, then a direct
`perl -MPOSIX` setsid/umask/`ready`/exec launcher, and keeps the PowerShell
supervisor only as the last resort (and on Windows). Semantics (`ready`
marker before exec, umask 077, scope env) are unchanged;
`test_process_supervision` gains a perl-only-PATH case, the mutation test
runs in 201 s instead of ~240 s, and the full script suite is green,
closing the "pre-existing cmp15ok/cmp4ok/cmp45ok" caveats recorded in the
S2, T3 and T4 notes below.

### T3. Nested-block derived operands typed before declaration replay

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-067 | #338 | `sizeof v[0]`, `sizeof s.q`, `sizeof *p` over a nested-block local report 2 |
| BUG-068 | #339 | `*q op= x`, `q[i] op= x` through a nested-block pointer to a 32-bit object add only the low word; `b[k]` through a nested-block `uint8_t *` reads a word |

**Confirmed root cause:** block-scope declaration spans are replayed into the
symbol table after the enclosing statement is lowered, so a base identifier
inside a derived expression has no symbol (`mir_ident_symbol() == NULL`) when
the statement is lowered. `sizeof` took the `TYPE_INT` fallback for the whole
operand; the `INDEXADDR` over such a pointer kept the stale `int *` node type
with `target_type == 0`, so the stride fix-up repaired the immediate but the
pointee repair then inferred `int` for the element load/store; and the
compound lowering narrowed the right-hand side to the assumed `int` target
exactly as BUG-063 did for the plain store.

**Fix:**
- `AST_SIZEOF_EXPR` whose operand derives from an unreplayed identifier is
  registered (`mir_register_deferred_sizeof`, bounded by
  `MAX_DEFERRED_SIZEOF`) and lowered as a deferred `MIR_VLA_SIZE` (flag 8);
  `ast_process_stmt_metadata` resolves it with
  `ast_sizeof_expr_value_replayed`, which retypes the operand bottom-up once
  the declarations exist. The VLA-size fix-up consults the resolved value.
- `mir_lower_lvalue_address` flags an `INDEXADDR` over a deferred pointer
  (`deferred_operand_types`); the stride-repair loop retypes it and its
  `memory_size` from the repaired base load so the pointee repair types the
  element access.
- The `*q`/`q[i]` compound path passes `pointer_deferred` as a deferred
  right-hand side and flags the narrowing unary (2048) so T2's
  `mir_retarget_store_conversion` retargets it.
- Five `dcc_mir_spilled_cfg.c` VLA exact-schedule hash pairs (`vla_in_loop`,
  `loop_stable`, `goto_stable`, two `nested_stable` orderings) replaced with
  the typed-shape hashes; a 601-file census shows only those functions owned
  the old hashes and only they own the new ones. Output is byte-identical
  for the four; `vla_nested` is shorter and now stores its `char` VLA as
  bytes.
- Packed-record campaign controls: expected FNV hash 1949229535 → 3306097631
  (padded variant 19367263 → 514719583) and the wave-20 `BASELINE_SHA256`
  refreshed; the host compiler confirms the new values, so main's controls
  encoded the BUG-068 read defect.

**Validation:** `bug067`/`bug068` FIXED in both modes; strict full+extended
runall (stack and no-stack) SUCCESS with 0 perf regressions, 146 of 561 rows
improve (tvla −0.58 %, tvlax −1.72 % peep); packed-record clobber cases
120/120 and wave-20 audit 3509 mutations clean; campaign baseline and exact
control unit tests pass except the pre-existing cmp15ok/cmp4ok/cmp45ok
(bisected to `523f5738`, see S2). Promoted as `tszderiv`
(`tszderiv,116548,117765,3840,4096`) and `tptrcasg`
(`tptrcasg,685899,690226,9088,9472`); `blockcassign` steer removed. New
BUG-069 (#341, two-dimensional member `sizeof`) logged with
`tests/known-bugs/bug069.c`.

### T1. Promoted-local slot hole reused under a materialized store

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-064 | #333 | Integer seed 261: bounded recursion through a function pointer grows the stack without bound |

**Confirmed root cause (not recursion):** in `f5` the spilled scalar-cfg
emitter promotes `l1` fully (no `MIR_LOAD`), so
`mir_promoted_local_slot_hole()` hands its frame slot `(ix-2)` to the
backend spill pool, skipping the object's own `MIR_STORE`s on the assumption
they emit nothing. The `h1_p` load (v144) is then spilled into that hole. But
`l1++` is still emitted by `mir_emit_selfstore_incdec()` as an in-place
`inc (ix-2) / jp nz / inc (ix-1)`, which increments the spilled function
pointer; `__CALL` enters `h1` at `h1+1`, after its `push ix`, and the
`pop ix / ret` epilogue unwinds into the caller's frame. ntvcm's
instruction trace showed 291 entries to `h1+1`.

**Fix:** the hole proof rejects an object when a retained store's source is a
fused self-store (`mir_value_is_selfstore_incdec`) or when a PHI over the
object is homed in its slot (`mir_phi_uses_scalar_object_home`). No emitter,
selector, schedule or runtime change.

**Validation:** `bug064` FIXED in both modes; generated assembly for all 591
existing test programs byte-identical against main `c3d46493`; MIR census
identical; all 545 existing perf rows unchanged; strict runall full+extended
SUCCESS; Python tool tests OK. Promoted as `tslothol`
(`tslothol,50581,55840,5760,6016`).

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

**Confirmed root cause:** MIR lowers every child of `AST_COMPOUND`, but the
metadata walk skips children after a guaranteed exit unless they contain a
re-entry label. A skipped nested declaration retains an unreplayed MIR
placeholder and unresolved object; no generated candidate accepts it. Support
probes also retain speculative scalar/VLA initialization instructions until
real declaration replay repairs them.

The chosen fix replays each dead child's metadata under the existing
assembly-suppression guard. It resolves declarations, neutralizes speculative
initializer instructions from support probes, and keeps scope/flow/VLA/debug
replay positions aligned with the unchanged MIR. The support walk also
neutralizes instructions from provably dead child probes while preserving
symbol/frame diagnostics: a VLA probe's renamed object can differ from real
replay, so name-based cleanup alone does not remove its bound side effects.
Labels and reachable code retain their existing paths; no lowering, emitter
or schedule changes remain.

Two lowering experiments were rejected. Removing every dead child changed
three `tsjdeep` functions because its exact `longjmp` schedule retains a dead
constant return. Preserving those returns made both censuses identical, but
skipping declarations left support-probe initializers at function entry:
`return 1; { int b = ++out; }` incremented `out`. Suppressed metadata replay
plus dead-probe neutralization fixes these defects without touching reachable
MIR or special-casing returns.
Both final censuses contain 3,489 existing functions with zero changes.
Strict full+extended stack/no-stack and Python regression gates pass; all
517 existing performance rows are unchanged. `tdeadblk` adds one measured row
and covers continue/constant exits, scalar/VLA side effects, nested/for
re-entry labels and declarations before later labels. Integer seeds 1-300:
220 ok, 79 INVALID, one target-limit-only; float seeds 1-300: 281 ok,
19 INVALID; zero FAIL in all production configurations.

The fixed return uses the same live instructions as its correct control.
Unreachable MIR remains: a repaired scalar add has three extra dead bytes
versus a literal-increment control, but no return-path cycle cost. CMake's
six sanitized MIR tests, focused debug/MIR tests, full-debug and release-
identical `-gline` pipelines, require-emit and 73 peep fixtures pass.
Debugger tests pass with one isolated retry of the MI endpoint-cleanup
assertion under load (9/10 initially, the remaining MI test passed alone).
The full regular Python run completed 319 tests with four failures, five
errors and two existing skips: two failures are approved #277; the other
failures/errors are mutation-fixture timeout/cleanup harness failures under
load. No expectations were fixed/skipped/refreshed. A Makefiles isolated retry
reproduced fixture timeouts and was stopped. The unchanged Ninja retry ran
all 14 mutation-harness tests: 13 passed, while
`test_default_serial_and_parallel_are_equal_and_bounded` timed out in its
first mock serial campaign. That one test timed out again when run alone
with Ninja; its serial/parallel mock-scheduler equality remains unverified.
These fixtures build synthetic CMake repositories, not the production
compiler. No harness, timeout, skip or expectation was changed.

Outside-cluster observation: BUG-022's parked program also passes because
its BUG-008 and BUG-019 emission-failure triggers are now both gone. This is
not a fix for its diagnostic masking: a scratch BUG-020 program preceded by
the same reused nested labels still exits zero with empty function bodies.
BUG-022/#242 remains open, steered and unpromoted.

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

**C1: BUG-027 and BUG-038 share one confirmed cause and are fixed** in
[#313](https://github.com/davidly/dcc/pull/313). A word
PHI correctly merges the assigned value, but a fused self-store reads the
named object's slot while PHI copies only leave the value in HL (or a backend
slot). `mir_phi_uses_scalar_object_home()` now materializes incoming edges in
the actual local/parameter home when the PHI's sole consumer is a store-only
fused adjustment. The result's sole-use guard prevents recursively following
loop-carried PHIs. No matcher, selector, cost policy, fallback or runtime
helper is changed. Unlike D2a, the PHI really identifies this object: what was
missing was materialization, not type/name metadata. D2c's byte-home liveness
proof is unchanged; B4's call-argument-cache PHI guard and B2a's narrow reload
handling are different roots.

First-root verification flips **027/038 only**; 029/037/052 remain wrong in
both production modes. BUG-057 also remains wrong and is not folded in. The
stop rule applies: no second independent fix is included. Before promotion,
known bugs are **3 FIXED / 15 fails** (the third FIXED is the pre-existing
BUG-022 fixture, whose diagnostic defect remains open).

Both original **3,558-function** MIR censuses are byte-identical, with zero
changed apps/functions in stack and no-stack mode. All **528 existing perf
rows** remain unchanged. The neighboring `no_update`, `no_outer_if` and
`no_else` function bodies are byte-identical in peep and nopeep. Corrected
PHI edges add just the missing word stores and retain the existing in-place
carry/borrow schedule (peep uses immediate IX stores). Strict Python
full+extended acceptance passes; CMake, compiler ASan/UBSan, required MIR
emission, both extended censuses, 31 focused optimizer/line-debug tests and
all 10 debugger-host tests pass. Full `-g` fixed fixtures run correctly in both
pipelines; `-gline` COM files are release-identical in both pipelines.

C1 is promoted as `tmerge16` and `tearly16`; only their new perf rows are
appended, preserving all 528 original rows and their order. tearly16 adds
nested-assignment/carry, decrement and no-early-return coverage. Both host
validations and strict focused stack/no-stack full runs pass. The restored
integer `nestedifelse`/`wordarm` seeds 1–300 are **293 ok / 7 INVALID / zero
FAIL** in all four production configurations. Float steering is unchanged.
After promotion: **16 parked reproducers, 1 FIXED / 15 fails**, with only the
known BUG-022 fixture exception. The bug log has **16 open / 39 fixed**.

Final censuses add 16 new test functions but change none of the original
3,558 in either mode. Strict Python full+extended after promotion passes
545 runnable apps, 24 existing skips, zero failures, including diagnostics,
extended, dccpeep and perf checks. Both strict PowerShell full+extended release gates also pass after promotion:
545 runnable apps and 24 existing skips in each stack/no-stack mode.

The regular Python gate runs **327 tests**, with the approved two #277
failures (`test_mir_campaign_baselines`, `test_mir_exact_controls`) and one
180-second synthetic mutation-harness timeout
(`test_default_serial_and_parallel_are_equal_and_bounded`). Its isolated
retry passes the serial phase but times out in the parallel phase after 180
seconds (359.704 seconds total); that single harness method remains
unverified under shared load. No expectations, skips or harness timing are
changed.
No historical compiler checks or prepush run is made.

A consumer-side PHI reload experiment was rejected: fusion intentionally
allocates no slot for this source, so it reads an unwritten virtual location.
Materializing the incoming edges is the correct producer/consumer contract.
A broad home proof recursed through loop PHIs; the store-only result guard
keeps that proof bounded without perturbing existing loop schedules.

**C2 was traced rather than guessed (029 fixed in C2a, 037 fixed in C2b; 052 remains open):**

- **BUG-029:** `dcc_mir.c:mir_compute_liveness_bytes()` and
  `mir_compute_liveness_bitsets()` stop their PHI-edge-input scan at the first
  non-PHI/non-NOP instruction. In `minus`, the byte PHI is instruction 19,
  its widening unary is 20, and the `x` PHI is 21. The latter's parameter
  operand is therefore not live on the incoming else edge: HL is overwritten
  by 100, then `dcc_mir_emit_common.c:mir_emit_homed_phi_copies()` copies that
  HL to DE as `x`. Without the cast the PHIs are contiguous and the control
  is correct. Forced homed reproduces it; forced spilled-all is correct.
- **BUG-037:** `dcc_mir_spilled_cfg.c:mir_binary_is_wide_phi_update()` checks
  only textually later uses of the old counter PHI. Its additional edge use
  in the earlier loop-header `l` PHI is missed. The in-place increment aliases
  the old/new counter slots; the backedge copy for `l` then reads the updated
  counter. A do/while or straight-line copy retains the original value.
  This is **not BUG-057**: it mutates a PHI backend slot before an edge copy,
  not a fused named-home result read by `mir_direct_wide_location()` in a
  direct wide addition. No direct-add consumer is present in the minimal
  BUG-037 functions.
- **BUG-052:** `dcc_mir_spilled_cfg.c:mir_prepare_backend_slots()` coalesces a
  conditional PHI with a solitary parameter source merely because it has one
  syntactic use, despite its live interval spanning the loop backedge. In
  production `f1`, parameter v0 (first 1, last 34), conditional PHI v15 and
  subtraction v17 share slot 0. Reusing that PHI slot for v17 also truncates
  its slot end to 28, allowing the counter's v20 to overwrite its low word.
  The next iteration therefore subtracts from the last result, not the
  original parameter. A local copy, a post-loop read, or no parameter store
  retains a separate/stable source and is correct. Forced homed is correct;
  spilled-all still fails `f2`. This is independent of C1's word self-store
  materialization and B4's call-argument cache.

### C2a. Interleaved PHI-edge liveness (BUG-029/#249)

The C1 trace was independently reproduced before editing: `minus` has byte
PHI 19, widening unary 20 and parameter-merge PHI 21. The byte and bitset
liveness scans stopped at unary 20, so parameter v0 had `live in=0 out=0`
at its definition and was not live on the else edge. Its HL home was reused
for the byte value 100; the homed emitter already walked all header PHIs,
but its edge copy therefore copied 100 from the clobbered HL into x's DE.
The no-cast control has contiguous PHIs 19/20 and keeps v0 live.

Both liveness scans now use the existing `mir_next_phi_in_block()` iterator
between PHIs, matching the emitter's block-bounded scan through interleaved
widenings and NOPs. Consecutive leading labels remain aliases; a later label
or terminator ends the scan. No lowering, selector, schedule, emitter,
runtime or fallback changed, and C2b's `mir_binary_is_wide_phi_update()` and
C2c's `mir_prepare_backend_slots()` are untouched.

`minus` retains `homed-scalar-cfg` and its original one-byte local frame:
x stays in HL and the byte uses DE before the merge, then the ordinary
parallel edge copies put l in HL and x in DE for `sbc hl,de`. This is the
same 38-instruction schedule already emitted for the no-cast control,
not a spill/fallback workaround. The existing no-cast and 16-bit controls
are byte-identical in both peep modes.

Debug review: this changes register live ranges/homes only for the formerly
broken shape, not source boundaries, scopes, types or symbol identities.
Full `-g` disables the affected promotion and keeps memory-backed variables;
`-gline` must remain release-identical with the corrected locations. Focused
incoming-parameter tests cover signed/unsigned byte widening and negation,
stack/release/line-debug/full-debug compilation and byte/bitset verification.

First-root verification: the original twelve reproducers report **2 FIXED /
10 fails**: BUG-029 alone flips in both production modes; BUG-022 was already
FIXED before this work, and its diagnostic defect remains open. BUG-037,
BUG-052 and BUG-057 remain wrong; no out-of-cluster reproducer changed.

Both original **3,603-function** MIR censuses are byte-identical, with zero
changed apps/functions in stack and no-stack modes, including a full
`DCC_MIR_LIVENESS_VERIFY=1` byte/bitset cross-check. All **534 existing perf
rows** remain unchanged. Strict full+extended stack-check and no-stack
regressions passed
(549 apps and 196 runnable extended apps per mode, diagnostics and peephole
fixtures, zero checked performance regressions).

ASan/UBSan compiled the reproducer with byte/bitset verification without
findings. All **35** focused arithmetic/debug Python tests and all **10**
debugger-host tests passed. Direct reproducer probes pass in release,
`-gline` and full `-g`, peep and nopeep; release/`-gline` `.COM` pairs are
byte-identical, line-debug location records and full-debug frame-variable
records are present, and function scopes remain balanced.

Restored integer `bytearm` seeds 1–300: **293 ok / 7 INVALID / zero FAIL**
across all four production configurations. No new bug or out-of-cluster
finding was uncovered, and no float generator was changed. BUG-029 is
promoted as `tphiarm` with its unchanged four-line host baseline; only its
new normal-stack measured row is added. Host validation and strict focused
stack/no-stack runs pass.

Full Python discovery ran **331 tests**: 324 passed, two skipped, two
approved pre-existing failures (#277: `test_mir_campaign_baselines` and
`test_mir_exact_controls`) and three synthetic mutation-harness errors.
The errors were the serial/parallel fixture's 180-second timeout, the
survivors/invalid-exits fixture's 180-second timeout, and a watchdog-child
marker not created before its deadline. Individual sequential isolated
retries passed the survivors fixture (147.966 seconds) and watchdog fixture
(6.249 seconds); the serial/parallel fixture again exceeded its unchanged
180-second timeout (182.829 seconds overall) and remains **unverified**.
These are mock scheduling/process harness checks, not compiler output
failures. No harnesses, expectations or skips were fixed or refreshed, and
no historical compiler or prepush checks were performed.

Final parked reproducers: **11 programs / 1 pre-existing BUG-022 FIXED /
10 fails**. The bug log and this plan consistently record **eleven open /
forty-four fixed** reported bugs. The sole new performance row is
`tphiarm`: **188369/188797 peep/nopeep cycles**, **5632/5632 bytes**. Every
original CSV row is byte-identical; the canonical baseline updater's whole-
file CSV reserialization was discarded while retaining the measured new row.
### C2b. Wide-counter PHI edge use

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-037 | #257 | A for/while body copies the old 32-bit counter before its update |

`mir_binary_is_wide_phi_update()` previously checked only textual uses after
an update. The loop-header `l` PHI is earlier in the stream, but consumes the
old counter on the backedge after the increment; sharing old/new slots makes
that edge copy read the updated value. This is separate from BUG-057's fused
named-home direct-add read and BUG-029/052's liveness/slot-coalescing defects.

The wide matcher now shares cluster N's exact earlier-used-PHI/CFG-live-out
proof with the narrow matcher in `mir_value_has_pending_earlier_phi_copy()`.
It guards both unit and non-unit wide updates. Unused PHI destinations emit
no copy and do not inhibit coalescing. No selector, profitability threshold,
`mir_direct_wide_location()`, MIR liveness, or solitary-PHI slot path changes.

- Both original census TSVs are byte-identical across **3,603 functions per
  mode**; all **534 existing perf rows** are unchanged.
- BUG-037 flips in both production modes. With the new BUG-058 parked,
  pre-promotion tally is **13 reproducers: 2 FIXED, 11 fails**; the only other
  FIXED is BUG-022's historical fixture-only pass. BUG-029/052/057 still fail.
  Post-promotion tally: **12 reproducers: 1 FIXED, 11 fails**.
- Promoted as `twphicpy`, retaining all six original lines plus stride,
  negative-counter, word-carry and zero-iteration cases. Only a new measured
  normal-stack-check perf row is added.
- The three original correct controls retain the same instructions in peep
  and nopeep, modulo label numbering. The selected spilled candidate remains
  unchanged; broken loops use native wide arithmetic and parallel edge
  copies, without a fallback/helper. Exact linked `for_loop` body size is
  **162→236 peep / 185→259 nopeep** (+74 bytes in the new failing shape only).
- Restored integer 1-300: **287 ok, 11 INVALID, 1 limit-only, 1 FAIL** (seed 43).
  Reduced in 86 tests to a loop-free cross-signed word initializer widened
  into a 32-bit array. Hand controls show scalar stores, function-scope
  locals, same-type initializers and separate assignments are right; both
  signedness directions and explicit casts fail in the array store.
  Filed independently as **BUG-058/#315**, parked and steered by
  `blockwordinit`; sibling compiler paths/BUG-015 steering remain untouched.
  `loopcopy` is restored; the remaining-known-shape proof passes 3,000 seeds.
  Repeating restored integer 1-300 after the new steer: **280 ok, 19 INVALID,
  1 limit-only, zero FAIL** across all four production configurations.
- Strict Python full+extended and both strict PowerShell stack/no-stack gates
  pass. Require-emit checks, both 274/274 extended censuses, two sanitized MIR
  verifier/bitset proofs, ten debugger-host tests and 40 focused Python
  compiler/generator/line-debug tests pass. The new steer's seven generator
  tests pass separately. ASan/UBSan compilation checks the original and
  extended fixtures with independent liveness/cache verification.
- Debug review: source boundaries/scopes/types are unchanged; corrected
  backend temporaries and instruction addresses use the normal metadata
  pipeline. All twelve expanded-fixture release/line/full-debug combinations
  pass; line-debug COMs are release-identical and full-debug retains named
  frame-variable metadata.

The full regular Python suite runs **330 tests: seven failures, three errors,
two pre-existing skips**. Two failures are the approved #277 campaign-golden
and exact-control gates; their expectations are not refreshed. The other
five failing methods and two timed-out synthetic mutation-fixture methods
are retried sequentially in isolation (seven methods, 398.253 seconds): all
five timing/process-cleanup failures pass. The serial/default scheduler
fixture and survivors/error-recording fixture still exceed their unchanged
180-second limits; the latter also reports a cleanup-directory race. Those
two harness methods remain unverified under shared load. No new compiler/MI
failure remains, and no harness expectations, timeouts or skips are changed.

The isolated retry methods are
`test_child_timeout_and_environment_isolation`,
`test_exited_worker_cleans_nested_process_scopes`,
`test_worker_watchdog_records_invalid_and_stops_descendants`,
`test_overall_deadline_and_explicit_stop_clean_live_processes`, and
`test_post_exit_grace_preempts_long_overall_deadline` (all pass);
`test_default_serial_and_parallel_are_equal_and_bounded` and
`test_survivors_invalid_exits_builds_and_worker_errors_are_recorded`
(remain timed out). No old-compiler/regression-dating checks or
`prepush.ps1` are run.

### C2c. Loop-live solitary conditional PHI sources (BUG-052)

The C1 trace independently reproduces: in production `f1`, parameter v0
(first 1, last 34), conditional PHI v15 and subtraction v17 share backend
slot 0. v0 has one syntactic use, but it is live out of PHI instruction 25
and survives the loop backedge at 34. The subtraction consumes the aliased
PHI slot and truncates its physical end to 28, letting counter v20 overwrite
its low word; subsequent iterations subtract from the previous result.

`mir_prepare_backend_slots()` now requires a solitary source to be dead
**on the CFG after the PHI**, using the existing live-out query, before
coalescing either incoming operand. The parameter retains its loop lifetime
and the conditional/subtraction use a distinct slot. This is an allocation
legality check, not a new selector, schedule, fallback or cost-policy gate.
No edits are made to C2a's liveness computation or C2b's wide-update matcher;
BUG-029, BUG-037 and BUG-057 still fail unchanged.

A first experiment requiring `last[source] <= phi_instruction` was rejected:
legal loop-header/backedge copies also have later linear interval ends,
and it changed 228 existing functions across 69 applications. Actual CFG
deadness preserves those copies: both original **3,603-function** census
TSVs are byte-identical (549 apps), and all **534 existing performance
rows** are unchanged. The original `ok1`/`ok2`/`ok3` instruction bodies are
unchanged in peep/nopeep after local-label renumbering.

The nine original reproducer lines now pass, as do six added lines for
nonconstant conditions (both arms), float/long conditional accumulators and
float/long struct-member stores. All 15 lines pass in both release/line/full
debug pipelines and forced `spilled-all` peep/nopeep; `-gline` COMs are
release-identical. The normal spilled selector remains selected: preserving
the live parameter costs copies and a larger frame only in these formerly
broken functions. Source scopes, types and statement boundaries are untouched;
changed temporary/frame locations are checked by the focused metadata and
ten debugger-host tests.


Restored float `fconstcond` seeds 1-300 across all four production
configurations: **291 ok, 9 INVALID, zero FAIL**; the remaining-known-shape
steering proof passes 3,000 seeds. No new bug is found. BUG-052 is promoted
as `tphislot`, retaining nine baseline lines and adding six. Before promotion:
**12 reproducers, 2 FIXED / 10 fails**; afterwards: **11 reproducers,
1 FIXED / 10 fails**, with only BUG-022's pre-existing fixture pass remaining.
Canonical/CMake builds, ASan/UBSan and six host MIR tests, both strict
full+extended release gates, strict Python full+extended acceptance, required
emission checks, both 274-function extended census modes, host validation
and 34 focused compiler/metadata tests pass. No old-compiler/dating check or
`prepush.ps1` is run, and no harness expectations, skips or timeouts change.


Regular Python unit gate: **330 tests, 7 failures / 5 errors, 2 pre-existing
skips**, in 1,069 s. The two golden-control failures are the approved #277
`test_mir_campaign_baselines` and `test_mir_exact_controls` findings; neither
expectation is changed. Nine other load-sensitive harness selectors are
retried in isolation after all this branch's long jobs finish: **3 pass;
6 remain unverified** (3 failures / 4 errors, including a teardown error).
These exercise mocked workers/process supervision, not changed compiler code.
No timeout, diagnostic expectation, scheduler, process cleanup or skip is
modified. The final restored-generator unit tests also pass (3 tests).

| Isolated selector | Result / evidence |
| --- | --- |
| `CompilerMutationTests.test_default_serial_and_parallel_are_equal_and_bounded` | unverified: 180 s mock-runner timeout; teardown directory still busy |
| `CompilerMutationTests.test_exited_worker_cleans_nested_process_scopes` | pass |
| `CompilerMutationTests.test_survivors_invalid_exits_builds_and_worker_errors_are_recorded` | unverified: 180 s mock-runner timeout |
| `CompilerMutationTests.test_child_timeout_and_environment_isolation` | unverified: startup/timeout loses expected child diagnostic |
| `CompilerMutationTests.test_worker_watchdog_records_invalid_and_stops_descendants` | unverified: watchdog fires before child PID file exists |
| `ProcessSupervisionTests.test_environment_is_inherited_and_policy_is_explicit` | pass |
| `ProcessSupervisionTests.test_completed_process_can_be_collected_with_zero_budget` | pass |
| `ProcessSupervisionTests.test_overall_deadline_and_explicit_stop_clean_live_processes` | unverified: child PID reported as 0 after the startup/deadline race |
| `ProcessSupervisionTests.test_post_exit_grace_preempts_long_overall_deadline` | unverified: 4.387 s exceeds the unchanged 3 s bound |

Only `tphislot` adds a measured stack-check row: **271,313 / 279,535 cycles**
and **8,448 / 8,960 COM bytes** (peep / nopeep). Its checked stack/no-stack
runs and optimized/ASan/UBSan host validation pass. All 534 old rows retain
exact values; BUG-022's fixture-only pass is ignored as pre-answered. No
out-of-cluster compiler finding is made or fixed.

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
| BUG-056 | #299 | Latent integer unary consumer of a float update value after admission |
| BUG-059 | #316 | Latent missing implicit float operand conversion after deferred update type repair |

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
(021/023: assignment-value lifetime)**, **D2c (024: byte-PHI lifetime)**
and **D2b (010/041: AST admission)**. D2b shipped only the safe long
admission for 010; **D2d (041/056: float update-value consumer typing and
admission)** remains stopped as recorded below. **D2e** splits the safe AST
update-provenance repair (D2e-1) from the still-unsafe float admission and
implicit binary conversion materialization (D2e-2, BUG-041/056/058).

#### D2c byte-PHI lifetime proof (BUG-024)

The byte case of `mir_phi_uses_scalar_object_home` previously admitted every
local byte PHI. In BUG-024, the store of the incremented value remains real
because the later do-while loads that local; the old header PHI is still live
when the store overwrites its reload home. Require CFG liveness to prove that
no materialized store of another value to the object occurs while the PHI is
live afterwards. PHI edge uses and backedges are already included in that
liveness. Address-taken objects and unavailable liveness decline the proof.
Dead stores are analysis-only and cannot clobber the home, so preserve them.
Word admission, selection and cost policy are unchanged.

The initial all-store prototype changed three existing functions in both
modes: `tbytepre.while_rollover_boundaries` and
`tpeepsum.compound_then_divide/compound_then_modulo`. Their update stores are
dead before any actual object load, so those were false lifetime kills.
Restricting the kill to a materialized store preserves all **3,503 existing
functions byte-identically in both censuses**. The parked BUG-024 passes all
four output cases in peep/nopeep; all other 24 reproducers still fail.

- The strict Python full+extended gate passes **534 apps** before promotion,
  including extended, diagnostics, dccpeep and checked performance. All
  **519 existing performance rows** are untouched.
- The fixed `signed_byte` body measures **246 -> 250 bytes nopeep** and
  **194 -> 174 bytes peep**. The four-byte nopeep cost belongs only to the
  previously incorrect reproducer. Its neighboring correct word-local body
  is **254 / 172 bytes**; the fixed byte case uses the same ordinary spilled
  and peep BC loop-value mechanism, with byte normalization as required.
  Manual word-local and no-do-while controls print the expected output in
  both pipelines. The initial manual control commands rejected a filename
  longer than CP/M 8.3; renamed worktree-local probes pass, but no separate
  before-fix control assembly comparison is claimed. Both censuses prove
  the existing neighboring schedules unchanged.
- Canonical and independent CMake builds pass; MIR host tests **6/6**,
  debugger-host tests **10/10**, focused integer/line-debug tests **24/24**
  and strict require-emit diagnostics pass. ASan/UBSan plus independent
  byte/bitset liveness verification pass BUG-024, tbytepre and tpeepsum
  stack-check probes. Full-debug target output and nonempty metadata pass;
  line-debug COMs are release-identical in both pipelines. The proof changes
  only immutable-value home eligibility, not source/scope/type boundaries.

- Restored byte-postfix integer production seeds 1-300: **266 ok, 34 INVALID,
  zero FAIL**. `postincvalue` is removed from `KNOWN_BUGS`; explicit disabling
  remains supported. Five generator tests and a **3,000-seed** proof of the
  remaining known-shape steers pass. No float generator changes/batch needed.
- BUG-024 is promoted as `tpost8` with the unchanged four-line host baseline;
  promoted host validation and normal-stack full-mode performance pass. Its
  only new row is **61,634 peep / 64,190 nopeep cycles**, **5,888 / 6,144-byte
  COM** respectively. All **519 original rows are byte-for-byte unchanged**.
- Final strict PowerShell full+extended stack and no-stack gates each pass
  **535 apps**, plus extended, diagnostics and dccpeep fixtures; checked
  normal-stack performance passes. Both final censuses contain **3,508
  functions**: tpost8's five are the only additions, and all **3,503 original
  function rows are byte-identical in both modes**.
- Final known-bug run: **24 reproducers, 24 fails, zero FIXED**. Counts agree:
  **24 open, 29 reported fixed** in compiler-bugs.md; fix-plan.md line 3 has
  24 open. BUG-024 alone flips; no out-of-cluster compiler fix or promotion.
- Initial full Python run: **319 tests**, two built-in skips intact;
  approved #277 failures in `test_mir_campaign_baselines` and
  `test_mir_exact_controls` remain untouched. One additional error is
  `CompilerMutationTests.test_default_serial_and_parallel_are_equal_and_bounded`,
  whose default serial fixture exceeds its **180 s** subprocess bound.
  Retried as the sole selected method (**1 test, 195.634 s**), it times out
  identically with clean teardown. That harness method remains **unverified**;
  no runner, fixture, expected result or skip was changed.
- Final post-promotion Python run: **319 tests, two skips**, the same two
  approved #277 failures, plus **five harness errors across four mutation-runner
  methods** (including a survivor-fixture teardown race), **840.720 s**.
  All four affected methods were retried alone, serially. The default method
  remains timed out as above; exited-worker cleanup (**11.504 s**) and worker
  watchdog (**9.142 s**) cannot find `watchdog-child`; survivor/error recording
  (**191.036 s**) again exceeds 180 s and races directory removal in teardown.
  **These four harness methods remain unverified**, not hidden passes. No
  compiler mutants, timing bounds, fixtures, expectations or skips were altered.
- No old-compiler checkout, regression-dating check, prepush run, or nested
  agent was used. Manual control source filenames are the only rejected
  probe setup; their renamed controls pass as noted above.

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

#### D2d stopped: float update provenance is lost at a binary consumer (BUG-041/056)

The first root-cause trial on `a8843888` repaired unary float-value typing
before restoring float admission: `mir_lower_simple_unary_chain` derived
unary `+`/`-` from the lowered float operand, and a corresponding pass after
lvalue/incdec metadata repair propagated float through dependent unary
instructions before resolving destination conversions. Casts and logical
`!` retained their own result types. The restored admission used a
non-recursive stored-lvalue float proof for prefix/postfix updates and the
float-value classifier; no emitter, selector, schedule or runtime changed.

That trial made **041/056 both FIXED** in peep/nopeep, including both parked
056 facets. It emitted IEEE sign-bit negation (`xor 128`) rather than integer
complement/add and narrowing. However, a defined-C dependent binary consumer
still emitted wrong code, so **the entire trial is withdrawn under the stop
rule: no float admission or unary typing change ships, no reproducer is
promoted, and 041/056 remain open behind `fincdecval`**.

Minimal failing facet (print `result` as IEEE bits through a union, as in
`bug056.c`; host clang and ASan/UBSan independently agree):

```c
float result;
{
    float local = 1.5f;
    result = -(local++) + (uint16_t)1;
}
/* expected result bf000000 (-0.5f), actual 40000000 (+2.0f). */
```

The stored update is correctly float. The MIR unary negation is now correctly
`type=5`, but the following **implicit conversion and addition still use
`type=34` (unsigned int)**, then convert that integer result back to float.
A matched member-array version in the same probe is correct (`bf000000`),
so this is not a float add runtime or unary emitter defect. Both peep/nopeep
and stack/no-stack fail the block-local form. Its Z80 converts the negative
float to an unsigned word and adds as a word instead of doing float addition:

```asm
ld hl,0
ld de,49088             ; -1.5f in DE:HL
call __ffu
ld de,1
add hl,de
call __fuf
```

Trace: `ast_types_deferred` descends ordinary unary `+`/`-`/`~`, but
`ast_expr_base_ident_unresolved` does not descend `AST_POSTFIX` or prefix
`TOK_INC`/`TOK_DEC`; an update rooted in an unentered block declaration loses
its unresolved-type provenance. `p_binary` therefore records int operand
conversion types without `deferred_types`, and `mir_lower_expr_impl` lowers
those stale conversions. Repairing the unary instruction later cannot
repair these unmarked consumers. A future root fix must preserve update-value
type/provenance through the AST and all dependent arithmetic consumers,
not merely retag the immediate unary instruction. No second fix was tried.

Trial verification:

- **20 parked reproducers: 3 FIXED, 17 fails**. Only 041/056 flip in this
  trial; 022 was already FIXED at the starting commit because B2b incidentally
  made its historical fixture pass. Its diagnostic defect stays open and is
  not changed here.
- Both strict **3,532-function** census TSVs are byte-identical to the initial
  canonical build: zero existing-function changes, in stack and no-stack
  modes. All **525 existing performance rows**, including row order, are
  untouched. No unit test or baseline is added/refreshed.
- Strict Python full+extended regression: **540 passed, 24 skipped, zero
  failures**, with extended, diagnostic, dccpeep and checked performance
  gates passed. Canonical build and independent CMake compiler build pass.
- Targeted compiler / line-debug tests: **31/31**. Debugger-host CTest:
  **10/10**, including both MI/compiler integration fixtures, passes without
  an isolated retry. The trial changes value types, not source/statement or
  scope boundaries; existing source-symbol locations and code hashes are
  unchanged. The new admitted consumer probe retains the same wrong result
  in full-debug and line-debug, in both pipelines. Its `-gline` binary is
  **not release-identical**: redundant `exx; exx` pairs appear around calls
  in both MAC outputs. This additional unshipped metadata/code interaction
  is recorded, not fixed or described as a passed new-shape contract.
- Full Python discovery and final strict PowerShell stack/no-stack release
  gates were not run: the wrong-code stop fired before promotion/release
  acceptance. The approved #277 failures remain untouched; no timing
  harness, expectation or skip is changed. No prepush or compiler-dating
  checks were run.

- Restored float production seeds **1-300: 288 ok, 11 INVALID, 1 FAIL**.
  Only **224** fails, in all four production configurations, with the six
  global-state checksum lines `400d` through `4012` differing. This is not
  called a clean batch. The failing seed reduces under nopeep to **81 lines
  / 104 reduction tests**; host clang + sanitizers prove its source/baseline.
  Its core is another dependent consumer of a block-local float update:

  ```c
  uint16_t g1 = 0xc217;
  { float b1 = 1.0f; fa[1] = (uint32_t)0 > ((b1++) - g1); }
  /* fa[1] should be 1.0f: 0.0f > -49686.0f. */
  ```

  Here the inner subtraction is repaired to float (`type=5`, `off2=5`),
  but an implicit cast to unsigned long remains between it and the outer
  comparison (`type=2`, `off2=36`). This confirms that repairing an update
  or its immediate unary consumer does not repair all dependent AST-typed
  conversions/comparisons. A nine-line scalar-destination version independently
  confirms host + ASan/UBSan **1**, dcc **0** in all four production
  configurations. No second compiler fix or generator steer is
  added for this stopped trial.

Final withdrawal verification: the canonical build passes after restoring
both compiler files and the generator byte-for-byte to `a8843888`.
**20 reproducers: 1 FIXED (pre-existing 022), 19 fails**, with 041/056 again
rejected by DCC-E1002 and every other original verdict unchanged. The only
committed file is `fix-plan.md`; counts remain **twenty open / thirty-four
fixed**. No issues are closed. All code, admission and generator edits are
withdrawn, with the stopped-trial patch and probe artifacts retained only
under `build/` for inspection.

#### D2e. Update provenance ships separately; float admission stops again

**D2e-2 follow-up (root repair):** late float typing now propagates through
unary `+`/`-`, deferred binary and conditional consumers after lvalue/update
repair. Missing implicit binary operand conversions are inserted there, while
explicit casts and all integer-only graphs keep their interpretation. The
non-update equivalents of BUG-059 and both A2b-2 float facets pass in
`tflbconv` (host clang + ASan/UBSan, peep/nopeep). Its eight printed cases
cover ordinary and assignment-value unary, mixed operands, compound values,
conditional consumers and a constant/cast chain.

Both **3,672-function / 559-app** starting census TSVs are byte-identical after
this root-only change. The **544 original performance rows** are preserved
byte-for-byte; only `tflbconv` is appended. All eight parked verdicts remain
`fails` at this step: BUG-059/041/056 are still admission-blocked, and the
integer-only `bug015.c` has **no changed output lines** (8/13 still wrong).
The float facets in the A2b-2 table are additional probes, not printed lines
in that parked integer fixture. BUG-015 stays open with A3/A4; its steering
and reproducer are unchanged. Float update admission is attempted separately,
after committing this shared repair.


**D2e-1:** `ast_expr_base_ident_unresolved()` now follows `AST_POSTFIX`
and prefix `TOK_INC`/`TOK_DEC` to the stored lvalue, just as it already follows
member/index/dereference and assignment results. A block-local update therefore
retains unknown-type provenance through unary expressions, mixed arithmetic,
comparisons and conditional consumers. Casts keep their authoritative types.
The new host-proven `tupdprov` exercises already-admitted wide updates, a
resolved global control, both update forms, operand order, comparisons and an
explicit cast fence. No float admission, MIR, emitter, selector, schedule or
runtime change ships; BUG-041 and BUG-056 remain open and parked.

**D2e-2:** the first provenance-plus-admission trial additionally derived unary
`+`/`-` types from their lowered float operands, reused deferred scalar type
repair after lvalue/update repair, and admitted float updates only through a
non-recursive stored-lvalue type proof. That made **041/056 both FIXED** in
peep/nopeep, including both parked 056 facets. Seeds 9 and 81 pass. However,
**restored float seed 224 still fails all four production configurations**.
The stop rule is followed: all admission and MIR changes are withdrawn, not
extended with another compiler fix.

The trace now differs from stopped D2d: the unary and dependent binary really
are float (`type=5`, `off2=5`), but an integer operand has no implicit float
conversion. Lowering emits no conversion for an operand already matching the
assumed common type; deferred repair only retags conversions that exist. An
explicit `uint16_t` cast remains authoritative, and its representation-identity
cleanup leaves an integer constant at the float binary. Integer-only deferred
constant widening cannot fix that. This is independently parked and filed as
**BUG-059 / [#316](https://github.com/davidly/dcc/issues/316)**; the exact
unshipped trial patch is on that issue. Future work must preserve and discharge
implicit operand conversion obligations after common-type repair, rather than
retyping explicit casts or pretending integer bits are IEEE bits.

```c
float result;
{ float local = 1.5f; result = -(local++) + (uint16_t)1; }
/* host bf000000 (-0.5f), D2e trial bfc00000 (-1.5f) */
```

The trial emits `xor 128` and `__faf`, but omits the integer-to-float `__fuf`
used by the correct parameter control. Prefix decrement, reversed operands and
chained arithmetic fail too; host clang + ASan/UBSan prove the parked baseline.
The original seed reduces to **81 lines / 110 reduction tests**. `fincdecval`
remains steered for **041/056/058**. No original out-of-cluster verdict changes;
BUG-022's already-FIXED historical reproducer stays with cluster M.

Trial verification before withdrawal:

- Restored float production **1-300: 289 ok, 10 INVALID, 1 FAIL (224)**.
  Only 224 fails, in all four configurations; this is not a clean batch.
- Trial integer cross-generator proof **1-300: 293 ok, 7 INVALID, zero FAIL**.
- Original **12 reproducers: 3 FIXED (022/041/056), 9 fails**. New 058 fails
  with wrong output in peep/nopeep; final shipped admission will reject it.
- Both strict **3,613-function** census TSVs are byte-identical to the initial
  canonical build; zero existing-function changes in either mode. All **535
  existing performance rows** are untouched.
- Strict Python full+extended gate: **550 passed, 24 skipped, zero failures**;
  extended, diagnostic, dccpeep and checked performance gates pass.
- Newly admitted 056 prints correctly in release, full-debug and line-debug
  nopeep. However, its line-debug binary is **not release-identical**, with
  redundant `exx; exx` pairs surrounding metadata at calls, as D2d recorded.
  That additional unshipped interaction is not fixed or claimed as passing;
  peep identity was not reached after the nopeep comparison stopped.

Final D2e-1 independent verification (the canonical release gates follow):

- Final isolated compiler: **13 reproducers, 1 FIXED (pre-existing 022),
  12 fails**. 041/056 and new 058 are rejected again; every original verdict
  is unchanged. None of their parked sources/baselines is moved or refreshed.
- Both final isolated compiler censuses preserve every one of the **3,613
  original function rows byte-for-byte**. Only the new `tupdprov:main` row is
  added in each mode. The 535 original performance rows, including their
  bytes and order, are unchanged; one new `tupdprov` row is appended:
  **102,812 / 102,976 cycles**, **5,504 / 5,504 bytes**, peep/nopeep, measured
  on the normal stack-check PowerShell path. Host validation passes.
- Both original `tflinc` MAC controls are byte-identical. New wide consumers
  use the incumbent generated MIR emitter; resolved constant results are
  `ld hl,1; ld de,65535`, with no word narrowing or new runtime/helper path.
- Final independent CMake/MIR host CTest **6/6**, ASan/UBSan compiler probe,
  targeted compiler/line-debug/AST/float-generator tests **32/32**, and
  debugger-host CTest **10/10** pass. The standalone new regression prints
  correctly under release, full-debug and line-debug in both pipelines;
  its `-gline` COM is byte-identical to release in both. AST provenance only
  affects deferred type tags, not source/statement or scope boundaries;
  existing symbols, frame/register locations and code hashes are unchanged.
Final blocked-shape float production health **1-300: 283 ok, 17 INVALID,
zero FAIL** after restoring `fincdecval`. Final integer production health
**1-300: 293 ok, 7 INVALID, zero FAIL** also passes on the canonical shipped
subset. This is deliberately not described
as restored admission coverage; the restored trial above remains a failure.

Canonical release verification also passes: both final censuses exactly match
those isolated snapshots, including the new function, and both strict
PowerShell full+extended stack/no-stack runs pass **551 apps, 24 skipped,
zero failures**, with extended, diagnostic and dccpeep gates passed. Normal
stack checked performance and the final new-unit validation pass.

Regular Python discovery on the canonical subset runs **333 tests**: the two
approved #277 campaign-assembly/exact-control failures remain, with exactly
the same failure evidence as the trial. One mock mutation-scheduler test
(`test_default_serial_and_parallel_are_equal_and_bounded`) additionally times
out at its existing 180-second fixture bound under load. A fresh-process
isolated retry **passes 1/1** after all long jobs finish, without changing any
harness, timeout, expectation, skip or compiler code. The two built-in skips
are unchanged. Initial trial discovery also had that timeout and a teardown
race (two errors); canonical discovery has only the one timeout error.
No additional Python/MI case remains unverified; #277 stays explicitly
pre-existing and untouched.

No BUG-041/056 reproducer is promoted and no issue is closed. Counts are
**thirteen open / forty-three fixed**, including newly filed latent BUG-059.
No prepush or compiler-dating checks are run.

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
| BUG-006 | #226 | **Fixed in E2c.** `mir_lower_expr_impl`'s `AST_ASSIGN` member compound path builds its initial `MIR_LOAD_INDIRECT` with `node->a->type == 0`, despite having the resolved `FieldDef`. `mir_emit_bitfield_extract` interprets missing `TYPE_UNSIGNED` as signed, so the top bit sign-extends. Repair explicitly excludes bit-field loads from pointee inference. Preserve the field's declared extraction type separately from its promoted arithmetic type here. |
| BUG-028 | #248 | **Fixed in E2-shift.** A discarded signed-byte shift had no promoted-left operation, so `mir_resolve_deferred_metadata` widened it to the unsigned word count type. `mir_lower_compound_value` now materializes promoted int and a separate narrowed result; equal-width counts cannot retype it. |
| BUG-002 | #223 | **Fixed in E2-shift.** `mir_lower_compound_value` left discarded byte shifts nominally byte-typed when a byte count did not widen them. Object promotion forwarded the word result through a byte return. Explicit promoted-left computation plus a distinct narrowed SSA/store value restores truncation. |
| BUG-004 (block facet; now A2b-1) | #225 | Fixed by replaying `mir_type_untyped_widened_value` when `mir_resolve_deferred_metadata` restores a canonical assignment conversion target. E1 fixed the function-scope half; A2b-1 fixes the block half without changing BUG-015. |

**E2-shift (BUG-002/028) is implementation-proven: one promotion/narrowing
root fix clears both.** No second independent metadata repair or subgroup
split is needed. Byte counts fail to widen a nominal byte operation; unsigned
word counts would give it unsigned arithmetic. Materialize the existing
promoted-left/narrowed-result form for those counts, retaining already-correct
signed-word and wide-count lowering.

Unconditional discarded-byte promotion was rejected: both censuses changed
21 reported function entries across `a1`, `tcmbyte` and `tlongcnv`, including
loss of `a1/op_rotate`'s exact schedule; many other entries just had renumbered
labels. Narrowing the same root fix to the count forms needing materialized
promotion makes both original 3,545-function TSVs byte-identical. No selector,
schedule, cost gate or fallback change remains. Each fixed body matches its
already-correct expanded-assignment control in peep/nopeep; both whole control
modules are unchanged. Additional temporary/frame cost is confined to the
previously incorrect new tests and matches the existing correct controls.

BUG-013, BUG-006 and BUG-004's remaining block facet stay open and steered;
their independent causes above are untouched. BUG-002/028 are promoted as
`tbcshl`/`tbcsar`, preserving all original host baseline lines and adding
bounded counts 0-15, boundary/sign values and consumed-result checks. Before
promotion: **19 reproducers, 3 FIXED / 16 fails**; after promotion: **17
reproducers, 1 FIXED / 16 fails**. The unchanged BUG-022 historical fixture
passes, as pre-answered; its diagnostic defect remains open. No other
out-of-cluster reproducer newly flips. Counts: **17 open / 37 reported fixed**.

#### E2-shift acceptance

- Restored integer `bytevarshl` and `bytesarcount`: production seeds 1-300,
  **297 ok / 1 INVALID / 2 target-limit-only / zero FAIL**. Seeds 82 and 178
  exceed CP/M TPA. Remaining steering proof: 3,000 seeds clean; generator
  tests 5/5. No float generator is affected and no float batch is needed.
- Both complete MIR censuses: **3,545 existing functions byte-identical in
  stack and no-stack modes**, zero existing selectors/apps changed. All
  **526 existing performance rows** remain unchanged, including ordering;
  only two normal-stack measured new rows are added.
- Strict Python full+extended acceptance: SUCCESS. Final strict PowerShell
  full+extended stack and no-stack gates: SUCCESS, each **543 passed / 24
  existing configuration skips**; extended, diagnostics and dccpeep gates
  pass. Both promoted apps pass their individual host validators and host
  ASan/UBSan baseline proof.
- Canonical and independent CMake builds pass; MIR 6/6, debugger host 10/10,
  focused compiler/integer/line-debug 44/44 and the new equivalence test 1/1;
  require-emit diagnostics and ASan/UBSan compiler/cache-verification probes
  pass. No statement/scope/declared-type changes; canonical conversion
  temporaries retain normal selected-frame location/address metadata. Both
  promoted apps pass all 12 release/line/full-debug pipeline combinations:
  `-gline` COMs are release-identical and full `-g` keeps correct output and
  nonempty adjacent DBG metadata in both pipelines.
- Full Python: **327 tests; failures=4, errors=3, two existing skips**. Two
  failures are the approved #277 campaign-baseline/exact-control goldens;
  they are neither fixed, skipped nor refreshed. Additional initial errors:
  mock default-serial/parallel and survivor-recording 180s timeouts, with a
  survivor teardown race; failures: process overall-deadline/explicit-stop
  cleanup and post-exit-grace timing. Each affected method is retried in a
  separate invocation: both process methods pass (4.683s / 5.470s), survivor
  recording passes (95.589s; teardown race disappears), but
  `test_default_serial_and_parallel_are_equal_and_bounded` still times out
  after 180s (192.915s total) and remains **unverified**. No harness, fixture,
  timeout, expectation or skip is changed. No old-compiler/dating checks,
  `prepush.ps1`, out-of-cluster fixes or merge.

The maintainer pre-approved **E2-bitfield** (BUG-013/#233 and BUG-006/#226)
as two independent, separately accepted single-bug commits in one PR, with a
stop rule per bug. BUG-002/028 remain a separate byte-shift workstream; neither
is changed here. BUG-004's repaired boolean conversion remains in A2b.

BUG-013's AST-only repair clears its parked program but restored integer seed
23 exposes the same lost field-range promotion in scoped MIR metadata repair:
`-3 > sign * (block_byte + unsigned_one_bit_field)` becomes an unsigned
comparison. Unary and deferred conditional repair also read raw field types.
Preserve the declared unsigned extraction type and apply field-range promotion
only at arithmetic consumers; explicit casts keep their own MIR unary view.
The seed now passes in all production modes and both 3,545-function censuses
remain unchanged. An earlier experiment promoting operands before array/pointer
decay changed four existing apps and was rejected; pointer decisions retain
raw object types and only arithmetic consumers use the promotion helper.

BUG-013 is accepted and promoted as `tbfexpr`: before promotion 2 FIXED /
17 fails (BUG-022's known fixture-only pass is unchanged); 300 integer seeds
with restored `bfpromo` give 297 ok / 1 INVALID / 2 target limits (82/178),
zero FAIL. Both 3,545-function censuses and all 526 incumbent performance rows
are unchanged. Strict Python and PowerShell full+extended stack/no-stack,
focused metadata, release-identical `-gline`, full-debug, require-emit,
six host MIR and ten debugger-host checks pass. BUG-006 remains independent.

The first regular Python gate ran 327 tests: eight failures, four errors,
two existing skips. Two failures are the approved #277 campaign baseline/exact
control failures. Nine unique synthetic-harness tests failed under load:
mutation scheduler equality, exited-worker cleanup, survivor/invalid-result
classification (plus its cleanup error), clobber lifecycle, watchdog cleanup,
and four process-supervision deadline/pipe/cleanup checks. Isolated retries
pass six of the nine tests (clobber lifecycle, watchdog and all four supervision
checks). Three mutation-harness checks were not verified by those isolated
retries:
scheduler equality and survivor/invalid classification each time out at the
unchanged 180-second fixture deadline (the latter also races cleanup), and
exited-worker cleanup still cannot find its expected watchdog-child file.
These are documented harness exceptions, not compiler failures; no fixture,
assertion, timeout, expected result or skip was changed.

The final standalone regular gate uses the existing Ninja build option and
runs **328 tests with only the two approved #277 failures and two existing
skips**; every earlier timing/cleanup failure, including all three checks above,
passes. No harness check remains unverified. The refined new test passes host
validation and all six release/full-debug/line-debug pipelines; normal-stack
cycles are 348370/356017 with peep/nopeep sizes 6784/7296. Both final incumbent
censuses still show zero changes across all 3,545 functions, and all 526 original
performance rows remain byte-identical. The final promoted corpus has six new
functions, not an incumbent output change.

**BUG-006 stopped under the per-bug output-preservation rule.** The member
compound path starts its bit-field `MIR_LOAD_INDIRECT` with AST member type 0,
so extraction sign-extends an unsigned field. Setting the load's declared
field type and separately promoting the arithmetic type clears BUG-006 and
corrects negative divisors, pointer/local fields, full-width right shifts and
live results. However, a full repair changes six existing functions across
`tbfinit`, `tbitfld` and `tcmwide`, including two exact-to-spilled regressions,
and is rejected.

A refinement preserves the existing low-bit-invariant shapes for add,
subtract, multiply, bitwise operations and left shift, repairing only division,
remainder and right shift. It keeps `tbfinit`/`tbitfld` byte-identical but still
changes `tcmwide.bit_field_through_pointer` from 434 to 410 instructions
(same generated candidate), with label-only changes in `array_member` and
`main`, in both censuses. This is an improvement, but any existing output
change is forbidden here, so it too is discarded. No selector, exact schedule,
performance baseline, known-bug steering or BUG-002/028 code is changed.

The incumbent `tcmwide` passes because `h.u = 444; h.u -= 5; h.u *= 5`
truncates to 147 before its wide division/remainder, leaving the sign bit clear.
The same function with initial `h.u = 200` reaches 463 before division and
prints 297 instead of host clang's 92; the narrowed repair prints 92 in peep
and nopeep. Clang O0/O2 and ASan/UBSan agree on that probe. Thus preserving
this function's original bytes would preserve the very same BUG-006 defect
for ordinary other inputs. A policy exception for that existing function's
output, rather than an application-name/value-specific workaround, is needed
to close BUG-006. Its original reproducer, baseline and `bfdivasg` steering
remain untouched; the final tree retains only the independently accepted
BUG-013 fix.


An out-of-cluster debug cleanup observation was also isolated: interleaving
`printf` inside the new scoped helper leaves two redundant `exx; exx` pairs
in nopeep `-gline` output, because metadata separates them during cleanup.
The unchanged branch-base compiler exhibits the same difference. No cleanup
rule is changed here; printing after the scoped helpers retains every checked
expression and all 15 host-baseline lines, and makes the delivered test's
`-gline` binaries release-identical in both pipelines. Only that new test's
normal-stack row is measured again; all 526 incumbent rows remain byte-identical.

No E2 implementation was included in E1. A2b-1 subsequently fixes BUG-004's
original block facet; restored fuzzing traces a separate byte SSA facet in
A2b-3. All five expanded issues remain open with their unresolved facets
steered.

#### E2c. Unsigned bit-field compound extraction (BUG-006/#226)

The maintainer explicitly approved reopening the stopped BUG-006 repair and
changing the incumbent `tcmwide` output. Its original 444 input truncates to
147 before division, hiding incorrect sign extension; input 200 instead
reaches 463 and gives 297 where host clang gives 92. Preserving those bytes
would preserve the defect. No application-name or input-value gate is added.

The member-compound `MIR_LOAD_INDIRECT` must retain the declared field type
for extraction. `mir_lower_compound_value` independently uses
`mir_promoted_operand_type`: unsigned widths <=15 promote to signed int;
width 16 stays unsigned. The final store still narrows and reloads the field,
so negative divisors, scoped right operands, full-width shifts and used
assignment results retain their C types.

**Refined repair chosen after a full-repair experiment.** Full extraction/
promotion repair changed six functions: `tbfinit.main` (302->1150 instructions)
and `tbitfld.main` (479->2835) declined their exact schedules for spilled
candidates; `tcmwide.array_member`, `bit_field`, `bit_field_through_pointer`
and `main` also changed. Those exact-to-spilled regressions are unacceptable.
Repairing only division, remainder and right shift retains every selector and
leaves `tbfinit`/`tbitfld` byte-identical.

This is a complete semantic repair, not a fallback: if a field has width w,
sign and zero extension differ by a multiple of 2^w. Addition, subtraction
and multiplication preserve that congruence; bitwise AND/OR/XOR independently
preserve each low bit; a defined left shift likewise preserves the low w bits.
The field store masks to those bits, and a used result reloads that mask.
Division/remainder (including negative divisors) and right shift do depend
on high bits and arithmetic signedness, and receive the repaired types.

Before extending the workload, both 3,613-function censuses change exactly
`tcmwide.bit_field_through_pointer` (434->410 no-stack instructions, 435->411
stack; 202 fewer assembly-text bytes), `tcmwide.array_member` and `tcmwide.main`
(label-only changes). All retain `spilled-scalar-cfg`, their incumbent candidate;
there is no newly spilled function or fallback. After extension, the exact
list remains those three functions, with `main` now adding the new coverage;
all other 3,610 existing functions remain byte-identical in both modes.

`tcmwide` retains the 444 input and also checks the 200 input, remainder of
463 by the wide divisor and right shift of 463. Host clang O0/O2/ASan/UBSan
agree with the revised baseline. The fix alone improves its original workload:
peep/nopeep cycles 136005/138544 -> 135869/138488; sizes stay 8192/8704.
The deliberately extended workload's checked row is
136005/138544, 8192/8704 -> **181731/185418, 8704/9344**.
The increase buys the additional execution, not slower repaired code.
Bounded extension experiments reduced three separate result prints
(237210/240682 cycles; 8704/9344 bytes) to one value print
(189250/192790; 8704/9216), then one combined correctness flag
(181731/185418; 8704/9344). Eager bitwise checks were slower
(181911/185452; same sizes), so short-circuit checks are retained.
The one-value-print option trades 128 fewer nopeep bytes for 7,372 additional
nopeep cycles; the delivered version favors minimum dynamic cost. The full
compiler repair was also tested and rejected as above. No selector/emitter
workaround or padding is introduced. All 534 other original perf rows retain
their exact bytes and ordering; only the new `tbfcomp` row is added.

The original BUG-006 reproducer flipped in peep/nopeep: 12 reproducers,
2 FIXED (006 plus the pre-existing fixture-only 022), 10 fails. It is promoted
as `tbfcomp`, retaining all six original lines and adding scoped negative
divisors, local/pointer fields, full-width unsigned shifts and live results.
All four host configurations agree; its normal-stack row is
177203/181128 cycles and 8192/8448 bytes. Both host validators pass. After
promotion: 11 parked reproducers, 1 historical FIXED (022), 10 fails.

**Debug metadata review:** only MIR value types and instruction addresses
change; declaration types, scopes and source boundaries are not rewritten.
Both tests pass all twelve release/full-debug/line-debug peep/nopeep pipelines;
`-gline` COM files are release-byte-identical. Full-debug frame variables and
line-debug ranged locations survive. Six MIR host and ten debugger-host CTests
and require-emit diagnostics pass. No debug or MI harness is changed.

The ordinary-assignment neighbour `a.u = a.u % divisor` is byte-identical
before/after (67 instructions, hash aa305755), as is that probe's `main`.
The repaired compound shrinks 76->68 instructions and uses the same zero
extraction and signed remainder sequence as its 67-instruction neighbour.
Its sole instruction difference is the pre-existing shared-address IY reload
(`push iy; pop hl` versus `ld hl,address`), not a new save or fallback; both
use the same byte count for that address load. Original peep and nopeep
reproducer assembly drops the sign-extension branch and uses `__r1s` /
`__mods` in place of `__r1u` / `__modu` for promoted narrow fields.

The final promoted censuses accept 3,620 functions (seven new `tbfcomp`
functions); their only three existing deltas are the exact list above.
Restored `bfdivasg`, production integer seeds 1-300: **293 ok, 7 INVALID,
zero FAIL and zero target limits**. No new bug was found or filed. The
pre-existing BUG-022 fixture-only pass is the sole out-of-cluster reproducer
observation; no other bug or steer is changed.

All strict release gates pass: PowerShell full+extended stack and no-stack
(575 apps: 551 pass, 24 existing skips), diagnostics and peephole fixtures;
Python full+extended (same app totals, 196 runnable extended cases), zero
performance regressions. The restored-generator/metadata/entrypoint focused
Python runner passes 22/22. Both host validators, all twelve debug pipelines,
6/6 MIR host CTests, 10/10 debugger-host CTests and require-emit pass.

The regular Python gate ran **333 tests**: **2 approved #277 failures,
4 harness errors, 2 existing skips**. The errors are the unchanged 180-second
synthetic mutation-fixture deadlines in
`test_default_serial_and_parallel_are_equal_and_bounded` and
`test_survivors_invalid_exits_builds_and_worker_errors_are_recorded`, each
with a directory-not-empty teardown race. Both are retried individually,
without altering any fixture, timeout, assertion, expectation or skip.
The isolated scheduler still times out after 180 seconds with the cleanup
race. The isolated classifier passes; the scheduler alone remains **unverified**
(181.256s total, two errors including teardown). This is an accepted harness
exception, not a compiler result; #277's two approved failures remain
unchanged. Logs: `build/e2c-python.log`,
`build/e2c-python-isolated-scheduler.log` and
`build/e2c-python-isolated-classifier.log`.
No old-compiler/regression-dating check or `prepush.ps1` was run.

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
only BUG-012 (**G1**); BUG-033/040/042 form **G2**. The maintainer has
approved G2 as three independent single-bug fixes (G2a/G2b/G2c), each with
its own commit and independent verification; the stop rule does not apply
between these three fixes.

**G1 — integer unary constant-expression admission.** The AST support gate
already admits `!` when `ast_const_scalar_fold()` proves the expression, but
rejects `-`, `+` and `~` over constant logicals or nested binary/unary forms.
Apply that existing foldability proof to all four unary operators, preserving
all preceding fast-path gates. MIR already lowers and folds these operations;
no parser, folder, MIR, emitter or runtime changes are needed. BUG-012's
original functions now emit byte-identically to their accepted literal
neighbors, with no instruction or frame cost.

**G2 — independently verified fixes:**

- **G2a BUG-033 — fixed:** `ast_const_plain_int_binary_supported()` explicitly declined a
  constant zero divisor; the integer folder correctly declines its evaluation.
  The generic constant-binary gate consequently rejected the expression.
  The admission fix retains the unevaluated `/` or `%` operation,
  as clang does, without inventing a result. The variable-numerator
  neighbor uses the nonconstant arithmetic path.
- **G2b BUG-040 — fixed:** the compact `parse_float_init_literal()` parser
  accepts only bare literals and narrow cast/product chains. Static data
  recording now falls back to a bounded temporary expression AST and typed
  host-float arithmetic. Existing compact and automatic initializer paths
  remain unchanged; new arithmetic emits data only, matching literal neighbors.
  Parenthesized/computed numeric casts use target-width range checks and
  truncation; private static evaluation does not admit function-body forms
  before G2c.
- **G2c BUG-042 — fixed:** the integer-only folder had no float result or
  float truth/comparison proof. Typed float and integer-result scalar proofs
  now admit these unary/logical/truth forms. MIR materializes only the newly
  admitted constants using existing literal candidates; single-unary literal,
  direct-comparison, cast and ternary paths and integer-only exit facts stay
  unchanged. Admission alone was rejected: it left redundant unary/logical
  MIR and an unnecessary IX frame; the typed literal lowering removes that
  cost without changing selectors or existing output.

G2a acceptance: **1 FIXED, 31 fails**, with no out-of-cluster flips. Both
censuses retain **3,465 existing functions byte-identically** with zero hash,
selector or metric changes. Strict Python full+extended is SUCCESS, and all
**511 existing performance rows are unchanged**. G2a's 300-seed integer
production fuzz is clean: **229 ok, 71 INVALID, zero FAIL**. The Python gate
ran 313 tests with the two approved #277 failures, two existing skips and
a load-related 180-second harness timeout and ensuing teardown race; an
unchanged isolated retry also timed out in its serial phase (180 seconds).
No harness code, skips or expectations were changed.
`-gline`/full-`-g` guarded operations preserve runtime helpers and diagnostics
are empty; focused line-debug tests pass. Promoted as `tconstdz`.

G2b acceptance: **1 FIXED, 30 fails**, with no out-of-cluster flips. All
**3,465 original functions remain byte-identical** in both censuses; only
`tconstdz` adds three functions per mode. All **512 existing performance
rows remain unchanged**. Strict Python full+extended is SUCCESS; all 23
focused optimizer/line-debug tests pass. Sanitized compiler probes pass.
The original reproducer's complete peep/nopeep MACs match accepted literal
neighbors byte-for-byte; twenty literal-data variants, ten numeric
cast/selection/sizeof IEEE-bit cases and eight arithmetic IEEE-bit cases
match clang and host
ASan/UBSan in both target modes. Promoted as `tflcini`; production float seeds 1–300 are clean: **288 ok,
12 INVALID, zero FAIL**. Both strict PowerShell full+extended stack/no-stack
gates pass; the extended census covers 274 functions in each mode. All
10 debugger-host tests pass.

G2c acceptance: **1 FIXED, 29 fails**, with no out-of-cluster flips. Both
ordinary censuses retain **all 3,465 original functions byte-identically**;
only promoted tests add functions. All **513 existing performance rows**
remain unchanged. The extended census retains all **548 rows** (274 functions
per mode) with identical hashes, selectors and metrics. Strict Python
full+extended is SUCCESS, and all 25 focused optimizer/line-debug tests pass.
New unary/logical/truth controls match their accepted literal neighbors in
release, stack-check and `-gline` output, without spill/frame overhead.
Clang and host ASan/UBSan prove nine extra facets; both target modes agree.
Sanitized compiler probes, independent release CMake build, MIR require-emit
contract, and all 10 debugger-host tests pass. Production float seeds 1–300:
**289 ok, 11 INVALID, zero FAIL**. Promoted as `tflcnot`.

Final G2 acceptance retains **511 original performance rows unchanged**,
with only three measured new rows (`tconstdz`, `tflcini`, `tflcnot`). All
**29 remaining reproducers fail** after promotion. Final ordinary censuses cover **3,470 functions per mode**,
with only five newly promoted functions and **zero changes to all 3,465
original rows**. All **548 extended rows** remain identical. Strict Python
application full+extended and PowerShell full+extended stack/no-stack gates
pass; all 25 focused optimizer/line-debug tests and 10 debugger-host tests
pass. Independent CMake and ASan/UBSan compiler checks pass. No G2 fix is skipped.

The full Python gate's approved #277 failures (`test_mir_campaign_baselines`,
`test_mir_exact_controls`) were not fixed, skipped or refreshed. The unchanged
mutation-runner fixture also timed out at 180 seconds in the original gate,
in an isolated default-generator retry and in an isolated Ninja retry; the
original/Ninja runs additionally raced teardown. The completed final Python
gate ran **316 tests: two approved #277 failures, three timing/teardown errors,
and two pre-existing skips**. Its additional exited-worker cleanup error
passes in isolation (22.6 seconds), as does the earlier child-timeout diagnostic
test. A G2b completion gate also saw survivor-fixture timeouts, watchdog timing
and process-supervisor grace assertions; each additional selector was retried
alone. Serial/survivor timeouts and watchdog/grace timing remain unverified
after unchanged default/Ninja isolation attempts. No harness code, skips or
expectations were changed. The full Python suite is therefore not green.

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

**Confirmed shared cause; both fixed.** MIR lowering is complete in both
cases. `mir_wide_helper_rhs_pair_consumer()` assumes the arithmetic left
operand was pushed at definition time. A parameter reload after a CFG join
is a direct named-home `MIR_LOAD`, not a `MIR_PARAM`; a negative float literal
is a rematerialized `MIR_UNARY`, not a `MIR_CONST`. Both definitions skip the
push, but the pair proof used to accept them, making the pending-stack
preflight invalid. Extend that single proof to reject direct named homes
and rematerialized wide constants. No AST admission, selection/cost policy,
new helper or fallback is needed, and no H1/H2 split is required.

BUG-020 is promoted as `tcmprhs`, including the float loop-bearing `if/else`,
braced early return and mixed-integer-constant facets. BUG-045 is promoted as
`tflncmp`, including unary spelling, signed-zero, positive-constant-left and
relation controls.
Before promotion, only 020/045 flip; all 20 original unrelated reproducers
still fail. The newly parked BUG-057 also fails; all 21 remaining parked
reproducers fail after promotion.

Both original **3,514-function** censuses are byte-identical in stack and
no-stack mode, and all **523 existing perf rows** are unchanged. Only the
new tcmprhs/tflncmp rows are added, measured through normal stack-check
PowerShell and host/sanitizer validated. Strict Python full+extended and
both strict PowerShell stack/no-stack release gates pass. CMake, compiler
ASan/UBSan, required-MIR-emission, debugger-host and line metadata checks
pass; `-gline` stays release-identical through peep/nopeep.

Fuzz: restored float seeds 1-300 are **283 ok / 17 INVALID / zero FAIL**.
The first restored integer batch is **251 ok / 48 INVALID / one FAIL**:
seed 52 exposes independent BUG-057 [#304](https://github.com/davidly/dcc/issues/304).
It is reduced, parked and narrowly steered, not fixed here. The full integer
rerun is **252 ok / 48 INVALID / zero FAIL**, and 3,000 steering seeds are clean.
No out-of-cluster parked reproducer flips.

**Out-of-cluster cause (B2c):** `mir_direct_wide_location()` cannot resolve a
fused wide-increment `MIR_BINARY` through `mir_direct_named_home_location()`;
`mir_emit_direct_wide_add()` then reads its unwritten/reused backend slot
instead of the incremented named home. No comparison helper-pair is involved.

**Python exceptions:** #277 (`test_mir_campaign_baselines`,
`test_mir_exact_controls`) is unchanged. Five synthetic mutation-harness
methods also hit timing/process-start failures; default isolation and Ninja
isolation were retried without any harness fix, skip or expectation refresh.
The two 180-second fixture timeouts and three child-start/cleanup checks
remain unverified under the shared machine load; exact results are in the PR.
No historical compiler checks or prepush run was made.

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


### R1. Normalize the restoring-division quotient before rounding

**Maintainer-approved runtime fix and numeric/performance expectation updates**
([PR #325](https://github.com/davidly/dcc/pull/325)). The maintainer ruled that:
1. Census invariance applies to compiler output for **unchanged sources**.
   Updating only the five embedded expected constants in `tstrb11.c` and
   `ttrg6sp.c` is an approved test-source edit; any changed existing function
   must belong to those two edited files.
2. Category-(b) monotonic mathematical accuracy is **not required**. Acceptance
   is correctly rounded IEEE division bits plus a per-line explanation of how
   the corrected quotient feeds the existing approximate libm, decimal parser,
   or `%f` formatter. Those independent implementations and their known limits
   are unchanged; the formatter defect is tracked as BUG-060 #322.

Correctness remains paramount; negative cycle/size impact must be minimal.
The bounded alternatives below retain the smallest shared-loop correction.

`fix/cluster-r1-fdiv`, based on `15db8e69`, changes only the shared division
runtime: the old 24-bit quotient was rounded before normalizing significand ratios below
one. One 25th restoring iteration, reusing `FDLUP`, normalizes before rounding.
The original hot loop is unchanged, and the post-round normalizer is removed.
The final shadow carry branches directly into rounding, removing its redundant
materialization in C. IY and both `__fdf` and stack `__fdiv` ABIs are preserved.
The helper's `__fdf`-to-`FSUB` span is **431 -> 420 bytes (-11)**.

Bounded tightening:
- Replacing the initially retained underflow `jp z` with in-range `jr z`
  saves one byte and three cycles on every below-one significand ratio.
- An alternate `ld a,d; or a; jp m` sign test costs one more byte. Peep
  probes, current bit-test -> alternate sign-test cycles: `tfdivr`
  10450289 -> 10450057; `tc89fmat` 99564 -> 99566; `mm`
  125251253 -> 125251353; `tmathf` 2955303 -> 2955288;
  `pihex` 689144124 -> 689145087. Retain the smaller bit-test, which is
  faster for the division-heavy mm/pihex workloads. These comparative probes
  are not substitutes for normal PowerShell checked-baseline measurements.
- Up-front 24-bit operand comparison/pre-normalization duplicates the existing
  restoring comparison and adds code; unconditional 25-bit division would
  require a wider quotient plus a separate rounding path for ratios above one.
  Neither improves on this bounded, shared-loop correction. Extra work for a
  below-one inexact significand ratio is necessary to recover the missing rounding bit;
  eliminating it reproduces BUG-039. No additional register saves were added.

#### Bit and ABI proof

The original known-bug program reports **FIXED in both modes**, 404/404 results.
A linked probe calls both entry points explicitly, setting/checking IY around
each call: **4404 pairs / 8808 ABI bit patterns per mode, zero differences**.
This includes the historical 4+20x20 table, another 4000 directed/random pairs,
minimum-normal-adjacent operands, just-below/above-one ratios, powers of two
for exponents 1..254, both signs and huge/tiny normal exponents. Normal-range
cases only: the existing target flush-to-zero policy is intentionally unchanged.
Host clang O0/O2 and combined ASan/UBSan agree. `tfdivr` persists the 404-pair
matrix through both ABIs with IY checks; its 809-line baseline is host-proven.
Both runtime audits pass, including all 174 mapped APIs and 24 formatted labels.
Raw reproducer peep/nopeep compiler assembly is unchanged by the runtime edit.

#### Approved numeric expectation changes

The maintainer approved all runtime-caused numeric updates below, in both
modes. Only **nine lines** change across `tfdedge.txt`, `trig.txt` and
`ttrig.txt`. `tstrb11.txt` and `ttrg6sp.txt` retain their original two-line PASS
outputs after the five embedded expected constants are updated; their baseline
files remain byte-identical. No other baseline or test source changes.
Category (a) is host-proven same-source arithmetic; category (b) explains the
corrected quotient feeding existing target libm/parsing/formatting. Mathematical
error figures are retained as context, **not acceptance gates**.

| App / line or check | Old -> corrected runtime | Class / corrected-quotient explanation |
| --- | --- | --- |
| tfdedge:5 | 14.285716 -> 14.285715 | (b) quotient bits match clang; clang `%f` prints 14.285714. Decimal error against 100/7 decreases, but formatter remains independently wrong (BUG-060). |
| tfdedge:6 | -5.666667 -> -5.666666 | (b) quotient bits match clang; clang prints -5.666667. Raw quotient error is halved; **decimal error increases** due to the existing formatter (BUG-060); this is explicitly accepted, not a divider-bit error. |
| tfdedge:7 | -5.666667 -> -5.666666 | (b) negative-divisor variant of line 6, same existing formatter behavior (BUG-060). |
| trig:11 | tanf 1.000000: 1.557449 -> 1.557448 | (b) true tan(1)=1.557407724654902; printed absolute error decreases from 0.0000412753 to 0.0000402753. |
| trig:15 | tanf 1.500000: 14.101420 -> 14.101419 | (b) true tan(1.5)=14.10141994717172; printed error **increases** from 0.0000000528283 to 0.000000947172; clang tanf prints the new value, explaining the changed runtime quotient feeding the unchanged approximate tan/formatter; monotonicity is not required. |
| trig:23 | tanf -1.000000: -1.557449 -> -1.557448 | (b) symmetric line 11; same improvement. |
| trig:27 | tanf -1.500000: -14.101420 -> -14.101419 | (b) symmetric line 15; same existing tan/formatter behavior. |
| ttrig:15 | tan 1.500000: 14.101410 -> 14.101412 | (a) identical float algorithm/printf line matches clang O0/O2 after renaming conflicting local math function names only; no arithmetic changed in the host source. |
| ttrig:27 | tan -1.500000: -14.101410 -> -14.101412 | (a) same proof, negative case. |
| tstrb11:finite low boundary | 00d9c7dc -> 00d9c7dd | (b) decimal parser. New value is the nearest float to 2e-38; absolute error decreases from 1.29908719234e-45 to 1.02211271922e-46. |
| tstrb11:integer cancellation | 3f7ffffc -> 3f800002 | (b) target decimal parser. Exact decimal is 1; both values have equal absolute error 2^-22, but opposite signs. |
| tstrb11:atof integer cancellation | 3f7ffffc -> 3f800002 | (b) identical parser proof. |
| ttrg6sp:tanf(+halfpi) bits | CBAE8A4A -> CBAE8A4B | (b) true tan(1.5707963705062866)=-22877332.42885646; error **0.42885646 -> 1.57114354**, from unchanged range reduction/approximation; the final division itself is correctly rounded. |
| ttrg6sp:tanf(-halfpi) bits | 4BAE8A4A -> 4BAE8A4B | (b) symmetric half-pi proof, same increased mathematical error. |

The half-pi library calculation's rounded range-reduction residual is
`b33bbd2e` (-4.371138828673793e-8). Its exact reciprocal is
-22877333.3265052307191; the corrected -22877334 is **closer to that actual
arithmetic operation** than the old -22877332 (0.673495 vs 1.326505).
This does not imply that the approximate library function is closer to true
tangent. The maintainer explicitly accepts that distinction and the independent
`%f` decimal-rounding defect without changing libm or formatting in this cluster.
The expected-constant source diff is minimal: `00d9c7dc -> 00d9c7dd` and two
`3f7ffffc -> 3f800002` occurrences in `tstrb11.c`; `cbae8a4a -> cbae8a4b` and
`4bae8a4a -> 4bae8a4b` in `ttrg6sp.c`. Both programs now print their original
PASS summaries and exit zero; no assertion is removed or weakened.

#### Gates and tracking

Before the runtime edit: strict stack full+extended passes 550 apps, 24 skips;
diagnostics/peephole/extended pass. Before the maintainer ruling, the correct
runtime necessarily exposed five obsolete numeric outputs/expectations and 22
checked cycle regressions. Those are now explicitly approved and updated.
The focused five-app numeric run passes in both modes. Only the **100 affected
apps** (95 previously reported changed passing-app rows plus the five numeric
apps) were remeasured using the ordinary stack-check PowerShell path; all 100
pass. No unmeasured row is refreshed.

Both final strict full+extended gates pass **551 apps / 0 failures / 24 skips**,
with **zero stdout failures** in peep and nopeep. Stack-check performance has
zero regressions after the approved 100-row refresh. Diagnostics, peephole
fixtures and the extended suite pass in both stack modes (50.47s / 50.69s).
Python rerun: **333 tests**, 2 skips, only the two pre-approved #277 failures
(`test_mir_campaign_baselines`, `test_mir_exact_controls`) and one serial/parallel
mutation-fixture timeout under load. That remaining timing case **passes in
isolation** (225.923s for the serial+parallel test). The other four previously
load-sensitive cases pass in full discovery. No Python harness is fixed, skipped
or refreshed; no additional unresolved failures remain after the retry.

Restored float production seeds 1-300 remain **283 ok, 17 INVALID, zero FAIL**;
neither the runtime nor generators changed after that clean batch.

The original censuses contain **3613 existing functions**: **3611 remain
byte-identical**, and only `tstrb11:main` and `ttrg6sp:main` change due to the
approved expected-constant edits. The new `tfdivr:main` and `tfdivr:show` are
additions, not changed original functions. Their unchanged committed source
was independently recompiled in both stack modes and matches the current
censuses exactly, so this ruling introduces no changes in the promoted test. The extended census preserves all
**548 rows** byte-identically. No selector, instruction count, block count or
schedule changes in the two edited existing functions; only expected immediate
values change. `tstrb11:main` emitted assembly text becomes eight characters
shorter because two `65532` low-word immediates become `2`; linked sizes do not
change. Debug statement/scope/register contracts are unaffected.

| Edited existing function | Stack hash old -> new | No-stack hash old -> new | Emitted-text bytes stack / no-stack |
| --- | --- | --- | --- |
| tstrb11:main | ff45189e -> 52365e2b | 9525f3c1 -> d043ba88 | 6415 -> 6407 / 6386 -> 6378 |
| ttrg6sp:main | 7f168cee -> dc4e2204 | 2bf17b89 -> 0282bd1b | unchanged |

New bugs logged and filed, not fixed: **BUG-060 #322** (decimal formatting of
explicit float bits, independent of division) and **BUG-061 #323** (`sizeof`
32768 sign-extension/signed constant division). Neither occurs in the float
fuzzer's bit-only observation or small-array shape, so no unrelated steer is
needed. Before promotion: **12 reproducers: 2 FIXED, 10 fails** (039 and the
pre-existing 022 flip). After promotion plus the two newly parked findings:
**13 reproducers: 1 FIXED, 12 fails**. BUG-022's incidental FIXED result is
left to cluster M.


#### Approved normal stack-check performance updates

**Exactly 100 existing rows change** below, measured only for affected apps
with `python scripts/runall.py -Apps <affected> -Mode full -UpdatePerfBaseline`
on the normal stack-check path. Every other existing row remains byte-identical.
The new `tfdivr` row from the initial R1 implementation remains unchanged:
20868763/20913550 cycles, 6016/6016 bytes (peep/nopeep).

Minimum-cost rationale keys, applying per row below:
- **C**: a below-one significand ratio requires its missing restoring step for correct
  rounding. The existing hot loop is reused without additional saves. The
  one-byte-larger alternate sign test is slower in mm/pihex; the retained
  `bit`/`jr` and short underflow branch are the bounded smallest choice.
- **R**: removing the post-round normalizer and redundant carry materialization
  reduces the ordinary rounding/dispatch path, improving these cycle counts.
- **F**: corrected quotients change downstream numeric/formatting data paths;
  their existing algorithms are unchanged. The five expected constants only
  correct assertions, retaining the test workload and PASS exits.
- **S**: the helper is 11 static bytes smaller, crossing 128-byte CP/M COM
  padding boundaries for these linked sizes. No compiler policy changes.

| App | Peep cycles old -> new | Nopeep cycles old -> new | Peep bytes old -> new | Nopeep bytes old -> new | Minimum-cost rationale |
| --- | ---: | ---: | ---: | ---: | --- |
| fcall1 | 115678 | 115799 | 5504 -> 5376 | 5504 | S |
| mm | 125169395 -> 125251357 | 126594011 -> 126675973 | 7936 | 7936 | C |
| pihex | 686738129 -> 689144476 | 689111117 -> 691517464 | 11136 | 11520 -> 11392 | C, S |
| primes | 3747236 | 3747247 | 6272 | 6400 -> 6272 | S |
| sieve | 16863183 -> 16857093 | 17306944 -> 17302294 | 5120 | 5120 | R |
| taggordr | 24165 | 24824 | 5504 | 5760 -> 5632 | S |
| tap | 167704902 -> 167807559 | 168241166 -> 168343823 | 7296 | 7296 | C |
| tarray | 2903240 | 2937554 | 8448 -> 8320 | 8448 | S |
| tasm | 9976 | 10008 | 5248 | 5376 -> 5248 | S |
| tatan2sp | 172284 -> 172192 | 172304 -> 172212 | 8704 | 8704 | R |
| tatof | 1773607 -> 1771348 | 1774477 -> 1772154 | 9216 | 9216 | R |
| tbig | 1237099265 | 1428808639 | 13568 | 13952 -> 13824 | S |
| tbitfld | 317747 | 320382 | 6912 -> 6784 | 7040 | S |
| tbits | 2129662 | 2134072 | 7552 -> 7424 | 7552 | S |
| tblkabi | 37257 | 37519 | 5760 -> 5632 | 5760 | S |
| tblkret | 138005 | 138310 | 5760 -> 5632 | 5760 | S |
| tboolbr | 29728 | 30388 | 5888 -> 5760 | 5888 | S |
| tc89bit | 35672 | 35996 | 5504 | 5632 -> 5504 | S |
| tc89decl | 37388 | 37803 | 5760 | 5888 -> 5760 | S |
| tc89fcmp | 24009 | 25084 | 6144 | 6272 -> 6144 | S |
| tc89fdiv | 40465 -> 40440 | 40482 -> 40457 | 6016 | 6016 | R |
| tc89fmat | 98829 -> 99626 | 98846 -> 99643 | 6656 | 6656 | C |
| tc89pp | 31262 | 31379 | 5376 -> 5248 | 5376 | S |
| tcanest | 23181 | 23395 | 5376 -> 5248 | 5376 | S |
| tcastasg | 72694 | 73097 | 5760 -> 5632 | 5760 | S |
| tclit | 50382 | 52016 | 7680 | 7808 -> 7680 | S |
| tcmfloat | 94411 -> 94388 | 94632 -> 94609 | 6016 | 6016 | R |
| tcmprhs | 184876 | 185304 | 8448 | 8576 -> 8448 | S |
| tconstun | 94472 | 94949 | 5504 | 5760 -> 5632 | S |
| tctxflt | 365602 -> 365556 | 372005 -> 371959 | 17792 | 17920 | R |
| tctxops | 102112 | 103173 | 10496 | 10624 -> 10496 | S |
| tdblnot | 105647 | 135090 | 6400 | 6784 -> 6656 | S |
| tdivzero | 70757 | 72044 | 9728 | 9856 -> 9728 | S |
| tdrive | 165292 | 165371 | 9344 | 9472 -> 9344 | S |
| terrno | 595114 | 598860 | 12288 | 12544 -> 12416 | S |
| texscan | 69716 | 69763 | 7936 | 8064 -> 7936 | S |
| tfdedge | 537327 -> 537355 | 537374 -> 537402 | 5376 | 5376 | C, F |
| tfdf | 91513 -> 89782 | 91533 -> 89802 | 6912 | 6912 | R |
| tflncmp | 123253 -> 123336 | 123991 -> 124074 | 7936 | 7936 | C |
| tfloat4 | 901241 -> 883480 | 906707 -> 888946 | 18944 -> 18816 | 19200 | R, S |
| tfmodfsp | 418762 | 419397 | 9984 | 10112 -> 9984 | S |
| tforfrm | 20245 | 20284 | 5248 -> 5120 | 5248 | S |
| tfpshad | 13502 | 13557 | 5120 | 5248 -> 5120 | S |
| tfpspec | 221338 -> 221292 | 222361 -> 222315 | 12672 | 12800 | R |
| tfseekr | 50079 | 50295 | 9984 | 10112 -> 9984 | S |
| tginaddr | 25609 | 25949 | 5504 | 5632 -> 5504 | S |
| tginoffs | 25475 | 25794 | 5504 -> 5376 | 5504 | S |
| thyp10 | 319455 -> 319409 | 320067 -> 320021 | 7936 | 8064 | R |
| tindopt | 39064 | 39430 | 5376 -> 5248 | 5376 | S |
| tindres | 40069 | 40349 | 5376 -> 5248 | 5376 | S |
| tinitfc | 25930 | 25950 | 5632 -> 5504 | 5632 -> 5504 | S |
| tinitreg | 40088 | 41626 | 6912 | 7040 -> 6912 | S |
| tinline | 237625 | 242787 | 6784 -> 6656 | 7040 | S |
| tkandr | 26010 | 26734 | 5760 -> 5632 | 5760 | S |
| tlimits | 119256 | 119273 | 5376 -> 5248 | 5376 -> 5248 | S |
| tlmul | 328896 | 330071 | 6912 | 7040 -> 6912 | S |
| tlog | 1009071 -> 1009961 | 1009128 -> 1010018 | 7936 | 8064 -> 7936 | C, S |
| tlogfsp | 155826 -> 155980 | 155846 -> 156000 | 7552 | 7552 | C |
| tmatha | 747800 -> 747754 | 748102 -> 748056 | 9984 | 9984 | R |
| tmathf | 2954551 -> 2955551 | 2956181 -> 2957181 | 17024 | 17024 | C |
| tmemaddr | 24423 | 24671 | 5504 -> 5376 | 5504 | S |
| tnarrow | 61833 | 61870 | 5376 -> 5248 | 5376 | S |
| tnddecay | 34622 | 35343 | 6144 | 6272 -> 6144 | S |
| tnegidx | 339578 | 342160 | 6784 -> 6656 | 6912 | S |
| tpeepsum | 148118 | 148193 | 6784 | 7168 -> 7040 | S |
| tpeeq8 | 306217 | 309981 | 5632 | 5760 -> 5632 | S |
| tpeesc | 117548 | 118773 | 6272 | 6400 -> 6272 | S |
| tpfbuf | 1480566 | 1706969 | 12288 -> 12160 | 12544 | S |
| tpflio | 540231 | 540858 | 8448 | 8576 -> 8448 | S |
| tphi | 1056643 -> 1058546 | 1056670 -> 1058573 | 5376 | 5376 | C |
| tpostidx | 18341 | 18358 | 5120 -> 4992 | 5120 -> 4992 | S |
| tpostinc | 234271 | 235053 | 6144 | 6272 -> 6144 | S |
| tppreg | 19462 | 19740 | 5504 -> 5376 | 5504 | S |
| tpromo | 161552 | 164544 | 9344 -> 9216 | 9344 | S |
| tptrdiff | 24153 | 24249 | 5504 -> 5376 | 5504 | S |
| tregnarw | 34709 | 34863 | 5248 -> 5120 | 5248 | S |
| trenwild | 229451 | 229624 | 8448 -> 8320 | 8448 | S |
| trig | 12837215 -> 12793704 | 12837574 -> 12794063 | 11648 | 11648 | R, F |
| trnb5 | 77177 | 77671 | 8704 -> 8576 | 8704 | S |
| trowptr | 22977 | 23944 | 5248 | 5376 -> 5248 | S |
| trtl2 | 57987 | 58424 | 9984 -> 9856 | 9984 | S |
| trw2 | 2038688342 | 2663408453 | 10496 | 10880 -> 10752 | S |
| trwold | 92122182 | 92139657 | 9216 | 9344 -> 9216 | S |
| ts | 768987 | 769014 | 6400 -> 6272 | 6400 -> 6272 | S |
| tsprintf | 207253 | 207875 | 7040 -> 6912 | 7040 -> 6912 | S |
| tsqrtsp | 208382 -> 209653 | 208402 -> 209673 | 7040 | 7040 | C |
| tsretmem | 58324 | 59278 | 5632 | 5760 -> 5632 | S |
| tstdlib | 124673 | 126616 | 9600 -> 9472 | 9600 | S |
| tstfield | 34146 | 34214 | 6272 -> 6144 | 6272 | S |
| tstr | 1343090011 -> 1343091356 | 1368263354 -> 1368265056 | 11136 | 11520 | C |
| tstraddr | 22963 | 23005 | 5248 -> 5120 | 5248 | S |
| tstrb11 | 2930351 -> 2931938 | 3036882 -> 3038469 | 9728 | 9984 | C, F |
| tszswap | 99488064 -> 99486414 | 133736936 -> 133734086 | 6144 | 6528 | R |
| ttrg6sp | 178917 -> 179533 | 179399 -> 180015 | 10368 | 10368 | C, F |
| ttrig | 40149112 -> 40303224 | 40199825 -> 40353951 | 15104 | 15232 | C, F |
| tunmsg2 | 40856 | 41229 | 5248 | 5376 -> 5248 | S |
| tvlax | 7721009 | 8564982 | 7040 | 7296 -> 7168 | S |
| tvplain | 74161 | 74578 | 7808 -> 7680 | 7808 | S |
| twhcomma | 228262 | 232963 | 8448 -> 8320 | 8448 | S |
| wumpus | 271445 | 293836 | 14592 | 15232 -> 15104 | S |


### S1. High-bit constant `sizeof` result typing

| Bug | Issue | Outcome |
| --- | --- | --- |
| BUG-061 | #323 | Fixed by the approved shared high-bit-only AST result-type rule; promoted as `tsizhigh` |
| BUG-062 | #327 | Residual small/runtime-size unsigned typing deviation; fixed in [S2](#s2-full-unsigned-sizeof-typing-with-recovery) |

Investigated on `fix/cluster-s1-sizeof-unsigned` from `8db72cbd`.
The initial full unsigned-type trial stopped at the existing-output gate.
**Maintainer ruling:** defer the full correction to a separately planned
**correct typing + exact-schedule recovery** track (alongside BUG-015's root
work), and ship only the representation-affecting constant-size range now.
The shipped rule is a deliberate interim C deviation, tracked by BUG-062,
not a claim that `sizeof` below `0x8000` has the correct standard type.
The log is **eight open / fifty-two fixed**.

#### Root cause and approved narrow repair

`p_unary()` in `src/dcc/dcc_ast_build.c` originally gave both `AST_SIZEOF_TYPE`
and `AST_SIZEOF_EXPR` the signed `TYPE_INT` result. The same module's
`ast_expr_type_for_sizeof()` independently returned `TYPE_INT` for both kinds.
The target's `size_t` is unsigned 16-bit. MIR lowering copies the AST type,
so typed folding correctly followed the erroneous signed interpretation:
bit 15 sign-extended on widening, division treated 32768 as -32768, and right
shift was arithmetic. The independent declaration/global-initializer constant
parser in `dcc_fold.c` already uses the correct unsigned type.

One small shared helper, `ast_sizeof_result_type()`, now serves all three
AST result-type sites. It returns `TYPE_INT | TYPE_UNSIGNED` only when the
known constant operand size is at least `0x8000`. Smaller or not-yet-known
sizes and whole-VLA runtime `sizeof` keep today's `TYPE_INT`. A comment points
to BUG-062 and the deferred full recovery. No MIR, constant-folder, schedule
or fallback-admission code changes.

The original reproducer and its expanded probe set pass peep and nopeep.
The 32760-byte neighbour remains unchanged. In **both modes**, the relevant
assembly is:

```asm
; before                         ; approved narrow fix
; (unsigned long)(sizeof(large) / 8)
ld hl,61440                      ld hl,4096
ld de,65535                      ld de,0
; (unsigned long)(sizeof(small) / 8): unchanged
ld hl,4095                       ld hl,4095
ld de,0                          ld de,0
; (unsigned long)sizeof(large)
ld hl,32768                      ld hl,32768
ld de,65535                      ld de,0
```

The push/call sequence is identical; fixing the reproducer adds no
instructions. Promoted [tsizhigh.c](tests/tsizhigh.c) is host-proven by clang
O0/O2 and combined ASan+UBSan and covers:

- 32768- and 65535-byte objects **and type names**;
- widening, unsigned division/right shift/comparison and negative signed
  comparison operands in the representation-affecting range;
- global constant initializers, typedef and local array bounds;
- a 35232-byte array of eight-byte pairs: `sizeof(pairs)/sizeof(pairs[0])`
  produces 4404 and the loop actually iterates 4404 times, not 61748;
- the 32760-byte passing neighbour and existing runtime unsigned `size_t`
  division, shift and comparison.

`sizeof` is not a valid operator in a C preprocessor `#if`: clang rejects
`#if sizeof(char[32768]) / 8 == 4096`, and `dcc_pp_expr.c` has no `sizeof`
evaluation path. That invalid-C surface is not a runnable oracle test.

#### Residual deviation and deferred full fix

New host/sanitizer-proven [bug062.c](tests/known-bugs/bug062.c), filed as
[#327](https://github.com/davidly/dcc/issues/327), fails in both modes:
small object/type `sizeof` and runtime VLA `sizeof` compare -1 as signed, and
a loop starting at -1 runs nine times for an eight-byte array instead of zero.
Explicit unsigned casts, global initializers and the repaired high-bit sizes
are passing controls. `scripts/clang_oracle_gen.py` has no `sizeof` nodes or
emitted expressions; no `KNOWN_BUGS` steer or generator modification is added.

The withdrawn full unsigned-type experiment changed selected hashes for the
same **173 of 3672 existing functions across 42 apps in each stack/no-stack
regular census**. Of these, 69 changed generated text-byte counts and 39
changed selectors, including **36 exact-to-spilled moves**. For example,
`tarray:test_many` moved from 5612 to 10045 text bytes and 566 to 980
instructions. These changes were not accepted; the list below belongs to the
**deferred full-typing recovery**, not the shipped S1 diff.

| App | Affected functions in the withdrawn full-type trial |
| --- | --- |
| `attnc11` | `add_outer_product`, `backward_pass`, `build_embeddings`, `cache_projected_qkv`, `check_sequence`, `convert_weight_group`, `count_predictions`, `cross_entropy_loss`, `forward_attention`, `generate_sample`, `infer_sequence`, `initialize_weight_group`, `load_weights`, `main`, `make_targets`, `process_sequence`, `process_sequence_file`, `project_all_qkv`, `project_cached_qkv`, `project_logits`, `report_training`, `run_training_file`, `save_weights`, `test_random_samples`, `transfer_weight_file`, `transfer_weight_group`, `transposed_matrix_vector_multiply`, `transposed_multiply_16x10`, `transposed_multiply_8x16`, `update_weight_group` |
| `bint` | `assignment_name`, `compile_line`, `compile_prog`, `dim_stmt`, `expr`, `factor`, `for_stmt`, `goto_line_op`, `if_stmt`, `isvarname`, `load_file`, `main`, `next_stmt`, `print_stats`, `print_stmt`, `relation`, `resolve_patches`, `run`, `split_lines`, `statement`, `sum`, `sym_find`, `term` |
| `cpmenumd` | `enumerate` |
| `forint` | `add_sym` |
| `pint` | `add_sym`, `add_var_name`, `assign_or_call`, `block_stmt`, `calc_code_limit`, `call_builtin`, `call_proc`, `case_stmt`, `consts`, `factor`, `factor_call_or_var`, `find_proc`, `for_stmt`, `if_stmt`, `init_run_storage`, `is_section`, `load_file`, `main`, `parse_call_name`, `parse_const_value`, `parse_expr`, `parse_params`, `print_stats`, `program`, `repeat_stmt`, `run`, `simple`, `statement`, `subprog`, `term`, `type_size`, `types`, `vars`, `xcalloc` |
| `t` | `ti16`, `ti32`, `ti8`, `tui16`, `tui32`, `tui8` |
| `tarray` | `main`, `test_many` |
| `tbool` | `branch_pair`, `check_branch_loops`, `check_branch_merges`, `check_branch_side_effects`, `check_branch_truth_table`, `check_deref_and_arrays`, `check_locals`, `check_operators`, `main` |
| `tbsearch` | `main`, `t_bsearch_edges`, `t_bsearch_int`, `t_bsearch_struct`, `t_bsearch_wide_comparator` |
| `tbufex` | `main` |
| `tc89size` | `main` |
| `tctrlz` | `main` |
| `texfile` | `main` |
| `texsort` | `main` |
| `texstrct` | `main` |
| `tfilero` | `main` |
| `tfio` | `main` |
| `tfopenw` | `main` |
| `tfpcall` | `main` |
| `tfreopen` | `main` |
| `tinitreg` | `main`, `tauto` |
| `tm1mu` | `main` |
| `tmodp2` | `main` |
| `tpadread` | `main` |
| `tpfargs` | `main` |
| `tptrinit` | `list_fold`, `list_free`, `list_prepend`, `list_reverse`, `main` |
| `tqsort` | `main`, `t_qsort_bytes`, `t_qsort_desc`, `t_qsort_int_edges`, `t_qsort_r5`, `t_qsort_str`, `t_qsort_struct` |
| `trw2` | `main` |
| `trwold` | `main` |
| `tsnprtf` | `main` |
| `tsparse` | `main` |
| `tstfield` | `add_word`, `find_word`, `main` |
| `tstr` | `main`, `test_wide` |
| `tstretst` | `main` |
| `tstring` | `main` |
| `tszlogic` | `main` |
| `ttmp` | `main` |
| `ttrig` | `main` |
| `ttypesr` | `main` |
| `tunbf` | `main` |
| `tunion` | `main` |
| `tvla` | `vla_sizeof_2d_rows`, `vla_sizeof_c99_type_counts`, `vla_sizeof_count`, `vla_sizeof_deep_nested`, `vla_sizeof_first_after_second`, `vla_sizeof_if_body`, `vla_sizeof_loop_changes`, `vla_sizeof_nested_block`, `vla_sizeof_op_add`, `vla_sizeof_op_and`, `vla_sizeof_op_mullhs`, `vla_sizeof_op_sub`, `vla_sizeof_saved_once`, `vla_sizeof_shadow_inner`, `vla_sizeof_shadow_outer_after`, `vla_sizeof_ternary` |

That full trial also changed eight of 548 extended rows: `00038:main`,
`00092:main`, `00093:main` and `00205:main` in both modes. The first three
changed only MIR instruction/value counts; `00205:main` also changed selected
assembly hashes and instruction/text counts. Their recovery remains deferred.

#### Narrow-fix verification and promotion

Both **3672-function regular censuses** (stack and no-stack) and the
**548-row extended census** are byte-identical for all existing functions
with the approved narrow rule. All **544 existing performance rows** and
stdout baselines remain unchanged. The only added performance row is the
normal stack-check measurement for `tsizhigh`: peep **1818029 cycles / 5888
bytes**, nopeep **1965332 cycles / 6016 bytes**.

The expanded BUG-061 reproducer was `FIXED` in both modes before promotion;
BUG-062 fails four of seven lines in both modes, and every other parked
reproducer still fails. `tsizhigh` passes the host promotion validator and
both target modes against its new stdout/performance baseline.

Final gates:

- Canonical all-tool build and the independent CMake compiler build pass.
- Both strict full+extended runs (stack and no-stack) pass **560 apps**, with
  **24 existing configured skips**, plus all **196 runnable extended apps**,
  diagnostics and dccpeep fixtures. The stack-check performance gate reports
  zero regressions; no-stack correctly omits checked performance comparison.
- The promoted test passes the host validator and normal checked peep/nopeep
  path. Final promoted-tree censuses preserve all 3672 old regular rows in
  both modes and all 548 extended rows; only two new `tsizhigh` function rows
  are added to each regular census. Final known-bugs result: **8 reproducers:
  8 fails**.
- Production integer fuzz, seeds 1–300: **283 ok / 14 INVALID / 3 limit-only /
  zero FAIL**. No generator changes or sizeof steering are needed.
- All six release/`-gline`/full-`-g` builds of `tsizhigh` produce the host
  baseline in both peep modes. `-gline` COM bytes are release-identical in
  both modes; metadata has valid headers and balanced function records.
  Debugger-host CMake/CTest passes **10/10 tests**, including metadata and MI.
- Full Python discovery runs **338 tests**: the two pre-existing #277 failures,
  seven timing/cleanup errors across six distinct cases, and two existing
  skips. All six affected cases pass isolated retries: five with the default
  tools; `test_default_serial_and_parallel_are_equal_and_bounded` still times
  out with the default Make generator but passes an additional isolated
  `CMAKE_GENERATOR=Ninja` run. The original Make-backed 180-second timing
  bound remains unverified. Neither #277 nor any harness is fixed, skipped,
  rebaselined or modified.

The timing retries cover `MirClobberRunnerTests.test_runner_contracts` and
`CompilerMutationTests.test_child_timeout_and_environment_isolation`,
`test_default_serial_and_parallel_are_equal_and_bounded`,
`test_exited_worker_cleans_nested_process_scopes`,
`test_survivors_invalid_exits_builds_and_worker_errors_are_recorded`, and
`test_worker_watchdog_records_invalid_and_stops_descendants`. The extra
initial error was teardown cleanup of the survivors fixture, not a seventh
independent compiler failure.

No out-of-cluster compiler defect is fixed. `prepush.ps1` and old-compiler
checks were not run, as instructed. The remaining gate caveats are #277's
known expectations and the unverified default-Make synthetic timing bound;
all compiler correctness, output/performance-invariance and debug gates pass.


### S2. Full unsigned `sizeof` typing with recovery

| Bug | Issue | Outcome |
| --- | --- | --- |
| BUG-062 | #327 | `sizeof` is unsigned `size_t` in every form; promoted as `tsizesgn` |
| BUG-065 | — | va_list offset not shifted with an IY register home; found during the trial, fixed, promoted as `tvaiyhom` |
| BUG-066 | — | `sizeof` of a nested-block scalar local reported 2; found by the first `sizeof`-emitting fuzz run, fixed, promoted as `tszblock` |
| BUG-067 | #338 | `sizeof` of an element, member or dereference of a nested-block local reports 2; logged with `tests/known-bugs/bug067.c` |
| BUG-068 | #339 | `*q op= rhs` through a nested-block pointer to a 32-bit object adds only the low word (sibling of BUG-063); surfaced by the changed fuzz stream, logged with `tests/known-bugs/bug068.c` and generator steer `blockcassign` |

Maintainer ruling after S1: do the full fix and claw the losses back through
optimisation, ideally improving ("I'd like this done right and for you to
then work to claw back losses and ideally improve"). Branch
`fix/bug062-sizeof-size_t` from `7dbe99d8`.

#### Typing

`sizeof` yields `size_t` (unsigned int) at all three construction/inference
sites; the S1 high-bit helper is gone. A new AST pass, `dcc_ast_sizeof.c`,
runs on each full expression just before MIR lowering and retypes a constant
`sizeof` subterm (value in `[0, 0x7fff]`) back to plain `int` exactly when
that is unobservable: every sign-sensitive operation that would see it
(ordered compare, `/`, `%`, `>>`, widening to long or float) has both operands
proven to lie in `[0, 0x7fff]` — from constants, narrow unsigned types or a
structural counting-loop proof — or already has a genuinely unsigned operand.
Unproven contexts keep the true unsigned typing. The re-census against
`build/census-main.tsv` went from 173 changed functions in the raw trial to
12, each inspected and justified.

#### Recovery

- Scalar-cfg emitter: stack-to-`BINARY` constant forwarding looks through
  `MIR_NOP`s; a planned-lhs rule forwards HL into a `BINARY` rhs; constant
  operands are popped early and `swapped_left_constant` avoids an `ex`;
  `& const` masks with a zero high byte fold to `ld h,0 / and K`.
- dccpeep: `pass_elim_param_only_ix_frame` removes the IX frame of a function
  whose only frame accesses are parameter loads (CFG SP-depth propagation
  rewrites them SP-relative); `push hl / pop de` copy forms become `ex de,hl`
  with exact `ex` liveness in `peep_dataflow.c`; the affine passes accept the
  copy form. Ten fixtures regenerated, 73/73 pass.
- Negative: loop-weighting the shared-slot access count in
  `mir_choose_iy_register_slot` (weight 2 or 4 inside back-edge spans) fixed
  `varjoin` but regressed 15 rows (tblkret, tcmdapp, tdecl, tforjmp,
  tforhome, tm, tnestfor, tpfargs, tflidx, tppasgn, trowptr, tstr, tunfphi
  among them); reverted. The selector cost model is static and pre-dccpeep,
  and the IY-slot chooser is a separate unilateral heuristic (shared-slot
  static access count >= 12) that no cost candidate arbitrates; a future
  recovery of `varjoin` should arbitrate it rather than reweight it.

#### BUG-065

Reproduced on `7dbe99d8` with a variadic join routine whose loop counter is
IY-homed: `mir_shift_frame_for_iy_register` moved locals, objects, slots and
`COMPOUND_ADDRESS` immediates but not the `MIR_VA_START/VA_END/VA_ARG`
immediate, so `va_start` wrote the saved caller IY at `(ix-2)`.
`mir_compound_address_needs_frame_shift` now shifts `VA_*` immediates with
negative offsets.

#### BUG-066 and BUG-067

The first integer fuzz run with the generator's new `sizeof` leaf failed five
seeds, all pre-existing on `7dbe99d8`: `{ uint32_t b = 1; *p = sizeof(b); }`
stored 2. Block-scope declaration spans are replayed after the enclosing
statement is lowered, so the operand identifier had no symbol and
`ast_expr_type_for_sizeof()` fell back to `int`; arrays were already right
because the deferred `MIR_VLA_SIZE` fix-up used the declared-object table.
The lowering now flags an identifier with no symbol (flag 16) and the fix-up
takes the declared size for any such non-VLA object. In the demotion pass the
same unresolved operand is no longer given a guessed constant; it uses the
noted simple scalar type or declines a value, and a whole VLA or unreplayed
local object is instead proven stack-bounded (`opt_stack_size <= 0x7fff`),
which keeps `sizeof a / sizeof a[0]` signed — `tvla` runs 36K cycles faster
than before the repair, and `tc89size` is unchanged. The derived-operand
shapes (`sizeof v[0]` of a block VLA, `sizeof s.q`, `sizeof *p`) still report
2 and are logged as BUG-067 rather than fixed here: they need the operand type
derived from the declared table at fix-up time, a separate piece of work.

#### Gates

- Both strict full+extended runs (stack and no-stack): zero failures;
  performance passed with **0 regressions and 255 improvements**; peep and
  nopeep both non-regressing. Three new rows: `tsizesgn,74485,74675,3328,3328`,
  `tvaiyhom,115015,119198,4224,4608` and `tszblock,50531,50790,3200,3328`.
- Campaign assembly: 39 `BASELINE_SHA256` values refreshed (every hunk shape
  categorised: `push hl/pop de -> ex de,hl`, dead IX store/reload removal,
  byte-half `and` masks, early pops, frame-slot shifts); four of them
  (compound-check/compound-wave45, gnarly-runner-wave100,
  raw-conversion-check-wave8000, nested-for-runner-wave8200) were already
  stale on main. `symbol-insert-wave22-audit.py` re-run: pass, counters
  unchanged. Campaign programs run end to end with identical output and
  equal-or-fewer cycles except `varjoin` (IY-slot threshold, see Negative).
- ASan/UBSan `dcc` over `tests/*.c`, `tests/mir-clobber/*.c` and known-bug
  sources and ASan/UBSan `dccpeep` over 605 `.mac` files: clean. Debugger
  host CTest 10/10; `-gline` COM bytes release-identical in both modes for
  13 programs.
- Script unit tests: all pass except
  `test_mir_exact_controls` cmp15ok/cmp4ok/cmp45ok, which fail identically
  on a clean `7dbe99d8` worktree (`compound-check-runner reject=shape` for
  `cmpw15.c`/`cmpw4.c` `main`); bisected to `523f5738` (2026-10-28,
  "Optimize boolean branches and preserve callback pointer argument
  types"). Pre-existing; not touched here.
- Integer fuzz seeds 1–300 with the generator now emitting `sizeof`: the
  first run failed five seeds (all BUG-066, pre-existing on `7dbe99d8`);
  after the fix one seed remained (193, BUG-068, unrelated to `sizeof`),
  steered as `blockcassign`. Final run `--off known`: **300 programs, FAIL 0,
  INVALID 5, ok 295**.
- Known-bugs runner: two reproducers, `bug067` and `bug068` (both fail, as
  expected).


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
so it matters more than its size suggests. Both are one-offs. The maintainer
approved M-a (009) and M-b (022) as independent single-bug commits in one PR;
the output-preservation stop rule applies separately to each.

#### M-a trace: BUG-009

MIR lowering correctly compares the updated `d` SSA value with the bound.
The selected spilled scalar CFG emitter fuses `load d; +1; store d` into
four ascending-byte in-place increments. Its ordinary wide virtual loader
already reloads this result from the named object home, but
`mir_direct_wide_location` only resolved loads/parameters: the relational
fast path fell through to the unused SSA backend slot. For the signed
reproducer it incremented IX-8..IX-5 and compared IX-22..IX-23 instead.
The fix reuses the existing self-store proof in the direct-location resolver,
keeping the same inline comparison and selector. In both peep and nopeep
assembly only the twelve operand offsets change across the three functions:
there are no added instructions, calls, frame bytes or fallbacks.

M-a acceptance: the re-established 21-program known-bugs set gives one
`FIXED` (009) and twenty `fails`; after promotion twenty parked programs
remain. Both 3,527-function censuses have zero existing changes, and all 524
existing perf rows are unchanged. Strict full+extended stack/no-stack and
Python full+extended gates pass. The 300-seed integer batch gives 255 ok,
45 INVALID, zero FAIL with `nestedwideless` restored. tndwide has a new
normal-stack measured row and passes host validation. Debug pipelines,
require-emit, ten debugger-host and six sanitized MIR host tests pass.
The 327-test Python gate has the approved two #277 failures plus two mock
scheduler timeouts and two existing skips. Individual Ninja retries still
time out; the survivor retry also has a cleanup error. Those harness checks
were unverified at the M-a checkpoint; final retry outcomes are below.
Nothing was skipped, refreshed or fixed.

#### M-b trace: BUG-022

The original proof used reused nested labels ahead of BUG-020's switch/
remainder comparison. H #308 fixed that language/codegen shape; the current
fixture instead initializes a local aggregate with `va_arg` in a variadic
function. Its last named parameter is int32_t (promotion-compatible on host
and target), and the host-proven stdout is `labels 1 2`, `third 7`.

Frame-sizing scans retain a previous function's label table long enough to
encounter a suppressed duplicate-label complaint. `g_diag_error_count`
intentionally counts even suppressed speculative complaints for constant
expression probes, but both required-MIR failure paths wrongly use its global
nonzero value to suppress their fatal diagnostic. The appropriate guard is
`errors`, the count of diagnostics actually reported to the user. This is
independent of BUG-009 and does not require changing label IDs or body output.
Aggregate va_arg initialization remains outside scope: diagnostic acceptance
requires the existing rejection, not admitting another unsupported shape.

M-b acceptance: the re-established trigger reports `FIXED` in both modes
by rejecting compilation, not by fixing the aggregate va_arg initializer. Before promotion the twenty
parked programs give one `FIXED` and nineteen `fails`; after promotion all
nineteen remaining programs fail. The negative source and host baseline are
in `tests/diagnostics/fixtures/`, tested by the regular Python diagnostic
matrix; the already-correct historical program is promoted as tlbreuse.
Its MAC/COM are unchanged by M-b in both modes and all debug pipelines pass.

The second integer batch gives 255 ok, 45 INVALID, zero FAIL with `duplabel`
restored. Combined M tally: 600 programs, 510 ok, 90 INVALID, zero FAIL.
Both final censuses preserve all 3,527 original functions and add only the
nine functions in tndwide/tlbreuse. All 524 original perf rows and all 525
rows present before M-b are unchanged; each new app has its own measured
normal-stack row and passes host validation. Both final strict full+extended
stack/no-stack gates pass, as do each fix's strict Python full+extended gate,
CMake, six sanitized MIR host tests, two sanitized probes, ten debugger-host
tests, require-emit and focused metadata/diagnostic checks.

The final regular Python gate runs 329 tests with five failures, six errors
and two existing skips. Two failures are approved #277; the other results
are seven loaded scheduler/supervision checks, including teardown races.
After heavy M jobs ended, each was retried individually with Ninja: six pass;
only the mock serial/parallel equality test still times out at 180 seconds.
That check remains unverified. No expectation, timeout or skip was changed,
and prepush was not run. The original BUG-020 trigger was superseded by H;
the replacement aggregate va_arg initializer is not fixed by this PR.

#### M follow-up on merged main 902aca64

A bounded search probed 1,584 arithmetic/control variants, 110 aggregate/
indirect-call/VLA variants and 32 ABI variants. A valid aggregate va_arg
initializer supplies a fresh production MIR decline; rejected oversized
analysis-budget probes were discarded because their early fatal path does
not observe the M-b guards. No diagnostic forcing or skip is needed.
The fixture rejects in all release/stack/line/full-debug modes, including
both strict flags. A CMake compiler copied under build/ with only the two
M-b guards reverted silently emits an empty third and exits zero for all
eight reused-label modes; renamed/flat controls remain diagnosed. The 48-case
proof has eight masks and forty correct rejections. The live compiler and
merged source were never reverted or replaced during the restored fuzz run.

BUG-057/#304 is the same M-a stale direct-location slot fault, shared by
addition and comparison. Promote it as twincadd, restore wideincadd, and move
the B2c ownership here. The restored 300-seed batch gives 290 ok, nine INVALID,
one target-limit-only, zero FAIL. Counts become twelve open / forty-three
fixed; after promotion all twelve remaining parked programs fail.
Both merged-baseline censuses retain all 3,589 existing functions and add
only seven twincadd functions. All 533 merged perf rows remain unchanged;
twincadd alone adds a new normal-stack measured row. Its host validation and
the replacement diagnostic matrix pass. Final build, twelve-reproducer
known-bug gate, strict full+extended stack/no-stack regressions (573 apps,
549 passed, 24 existing skips), and all 44 tests in the four requested
Python modules pass. The first after-census attempt overlapped the canonical
linker replacing ./dcc and failed to launch it; an unchanged retry after the
build completed passed both modes. No timing, expectation or skip changed.
The exhaustive Python discovery/prepush suites were not rerun; previously
documented #277 and the mock serial-scheduler timeout remain outside scope.

### N. Lost variable-index local float-array store

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-055 | #294 | Variable-index local float-array store in an `else` arm after a counter-copy loop |

**Traced cause:** the store and its address lowering are intact. The first
loop's `l4 = i1` becomes a second PHI consuming the old counter on the
backedge. `mir_binary_is_narrow_phi_adjust()` checks only textual uses after
the increment, overlooking this earlier PHI's edge use. At 40 CFG blocks it
updates the counter's slot in place, so the backedge copies `5`, not `4`,
and the later store addresses element 1 instead of 0. Removing the unused
conditional expression drops the CFG below that profitability threshold
and retains separate counter/update slots. A constant index bypasses the
bad counter; changing only the store to the then arm or to an `int32_t`
array still fails, contrary to the proposed neighbouring-shape assumption.

The narrow matcher retains its existing profitability/legality gates and
adds the existing CFG-live-out proof for earlier PHIs whose destinations
are used. Unused PHIs produce no edge copy, so they must not disable the
already-correct constant-index control schedule. The wide
matcher and BUG-037 remain outside this task. Debug scopes/types/source
boundaries are unchanged; corrected temporary homes and instruction
addresses require the normal full/line debug pipeline checks.

- Restored float production seeds 1-300: **283 ok, 17 INVALID, zero FAIL**;
  the `farrloopindex` known-bug steer is removed. Remaining-known-shape
  steering proof: 3,000 seeds clean. No new bug was found.
- Promoted as `tflidx`, preserving all original output lines and adding
  then-arm, integer-array and decrement coverage. Post-promotion parked
  tally: **15 reproducers, 1 FIXED, 14 fails**.
- Both census TSVs are byte-identical across **3,574 existing functions**;
  all **530 existing performance rows** remain unchanged. Strict Python
  full+extended and both PowerShell stack/no-stack release gates pass.
- Pre-promotion parked tally: **16 reproducers, 2 FIXED, 14 fails**. Only
  BUG-055 newly flips; BUG-022's historical fixture remains the pre-existing
  pass while its diagnostic defect stays open. BUG-037 still fails.
- Constant-index and reduced-CFG control instruction streams are unchanged
  in peep/nopeep (only whole-program label numbering moves). The failed
  shape retains `spilled-scalar-cfg`; its counter update and parallel edge
  copies now match the reduced-CFG control, without a fallback selector.
  Exact linked `f0` body size is 713→732 bytes peep (+19) and 815→838
  bytes nopeep (+23); its 34-byte frame is unchanged and no helper is added.
  That cost is confined to the previously wrong new shape.
- Canonical/CMake builds, MIR host 6/6, debugger-host 10/10, focused compiler
  30/30, line metadata 2/2, require-emit diagnostics and ASan/UBSan with
  independent liveness/cache verification pass. The extended MIR census
  is 274/274 (100%) in each stack mode. All twelve new-fixture
  release/line/full-debug combinations (both peep modes and stack modes)
  pass; line-debug COMs are release-identical and adjacent DBG metadata is
  nonempty. Source scopes/types/boundaries are unchanged; corrected frame
  temporaries and instruction addresses use normal emission metadata.
- An initial overlapping-build Python suite had a transient diagnostics
  failure only; all 116 diagnostics pass in isolation and the complete
  final strict rerun passes. No diagnostic expectations were changed.
- Regular Python unit gate: **328 tests, 2 existing skips**, the two approved
  #277 golden failures, a mock serial/parallel mutation-runner 180-second
  timeout, and a MIR-clobber lifecycle timing failure. The MIR-clobber
  method passes in isolation. The mock scheduler still times out at 180
  seconds alone (plus a teardown race); the complete isolated Ninja
  retry passes the same assertions; no harness/timeout/expectation
  or skip was changed. No old-compiler/dating or `prepush.ps1` checks run.

## Suggested order

1. **K, L** — isolated, cheap, four and one issues; good warm-up for the
   acceptance loop.
2. **A1** — merged (#281). **A2a** — fixed and verified.
   **A2b** is deferred until after E.
3. **B1** (BUG-030 memory corruption), **B3** (BUG-026, maintainer's first
   priority), then remaining **B4**; **B2a** and **B2b** are verified separately.
4. **D** — BUG-044 and BUG-003.
5. **E**, then **A2b**, then **C** — E and C are in the emitter's
   typing/merge paths; A2b waits for E's BUG-004 facet; C is the least
   certain cluster.
6. **I1**, then **I2** on an independent branch; **J, H, G, F, M** in any order.

### F1. Exact-bit fixed-decimal rounding

**Maintainer-approved runtime correctness and stdout/performance updates.**
[PR #330](https://github.com/davidly/dcc/pull/330), branch
`fix/cluster-f1-printf-rounding`, based on `8db72cbd`; closes BUG-060
#322. Only `DCCRTL.MAC` changes toolchain behavior. No compiler or math-library
function is changed. The old formatter added a rounded binary float half-unit
before splitting, then repeatedly multiplied a rounded residual by ten. The
new shared `%f` handler represents the exact significand in a little-endian
decimal vector, scales by appending precision zeros, cancels exact powers of
two, and divides/doubles by the binary exponent. Its last discarded binary
bit is the guard; earlier discarded bits form sticky. It rounds exactly once,
ties to even, including decimal carries, signed zero and subnormals.

The `_pffio`-to-`_pflng` span is **733 -> 731 bytes (-2)**. The transient
vector is precision+9 bytes below 2^23, or 39 bytes for exact large integers;
internal precision caps at 149 (all binary32 fractions terminate there) and
requested trailing zeros extend through precision 255. IX is locally
saved/restored; IY is untouched; BC's printf character count is restored
before output, then retained through cleanup. No new public helper/dependency
is introduced; the existing `pf_divu32_10` and decimal-doubling idioms are reused.
Removing float arithmetic from formatting also lets the existing stripper
remove unused float helpers from many linked images.

Bounded alternatives, linked peep probes (cycles include output):

| Sequence | Original bug060 | tpfio (normal stack path) | Formatter span |
| --- | ---: | ---: | ---: |
| Original rounded-float formatter (incorrect) | 240726 | 390059 | 733 |
| Exact decimal, branchful half-digit carry | 249808 | 565361 | 683 |
| Exact decimal, branchless `sbc a,a; and 10; ld e,a` | 245558 | 557059 | 681 |
| **Retained: branchless + exact trailing-bit cancellation** | **212812** | **241813** | **731** |

The branchless carry costs two fewer bytes and avoids a data-dependent branch.
Canceling exact binary factors adds 50 bytes to that candidate but avoids up
to 23 full decimal division passes for common exactly representable values.
The retained correct implementation is smaller than the old formatter and
substantially faster on the focused formatted-I/O workload. A float-based
correction or a fixed guard epsilon cannot distinguish true decimal ties and
would retain the root double-rounding error; a general bignum/64-bit library
would be larger than this bounded vector.

Proof: `python3 scripts/printf-float-proof.py` links and runs **6000 exact
finite patterns** (5972 normals, 26 subnormals, two zeroes; 2999 negative)
in each mode at `%f` and `%.Nf`, N=0..6: **48000 matching
conversions/mode**. Patterns cover all finite biased exponents, randomized
mantissas and signs, halfway ties, 0.999999x carries, five neighbors around
powers of ten, max/min normals, zeroes and subnormals. Another **227 patterns**
at precisions 7/9/16/32/64/149/150/255 add **1816 matching conversions/mode**:
**99632 total, zero mismatches**. `%e`/`%g` are documented, pre-existing
unsupported conversions: they emit literal `e`/`g` without consuming arguments;
those fields are checked separately, not misrepresented as host agreement.
The unchanged special-value/width/sink behavior is covered by existing printf
regressions; new `tfpround` adds focused ties/carry/subnormal checks.

Both strict MIR censuses preserve **all 3672 existing function rows byte for
byte**, in stack and no-stack modes; only `tfpround:main` is new. Original
bug060 peep and nopeep application `.MAC` output is byte-identical before/after.
Runtime IY-safety and API/label coverage audits pass. The known-bug runner
reported only bug060 FIXED before promotion; seven other reproducers still fail.
BUG-060 is promoted as `tfpround`; current tally: **seven open / fifty-two fixed**.
No formatter feature exists in the float fuzzer's KNOWN_BUGS: it compares raw
bits, so no generator steering is changed.

#### Every approved stdout baseline update, with clang proof

A linked instrumented copy of the runtime captured exact original input bits
before formatting; host clang `printf("%.*f", 6, promoted_float)` prints each
new value below. In multi-float lines all fields were checked, not just the
changed last digit. These are formatter corrections to unchanged dcc values,
**not claims that dcc libm or the application's xsinf approximation equals
host libm**; those existing approximation differences remain out of scope.

| Baseline:line | Old complete line -> new complete line | Host clang `%f` for exact bits |
| --- | --- | --- |
| tfdedge:5 | `14.285715` -> `14.285714` | `41649249` -> `14.285714` |
| tfdedge:6 | `-5.666666` -> `-5.666667` | `C0B55555` -> `-5.666667` |
| tfdedge:7 | `-5.666666` -> `-5.666667` | `C0B55555` -> `-5.666667` |
| tfmedge:8 | `15.185090` -> `15.185089` | `4172F620` -> `15.185089` |
| tfmedge:9 | `8.539722` -> `8.539721` | `4108A2B3` -> `8.539721` |
| tphi:11 | `  at 10 iterations: 1.617978` -> `  at 10 iterations: 1.617977` | `3FCF19E3` -> `1.617977` |
| trig:15 | `tanf  1.500000: 14.101419` -> `tanf  1.500000: 14.101418` | `3FC00000` -> `1.500000`; `41619F69` -> `14.101418` |
| trig:27 | `tanf  -1.500000: -14.101419` -> `tanf  -1.500000: -14.101418` | `BFC00000` -> `-1.500000`; `C1619F69` -> `-14.101418` |
| trig:56 | `acosf -0.250000: 1.823476` -> `acosf -0.250000: 1.823477` | `BE800000` -> `-0.250000`; `3FE967AE` -> `1.823477` |
| trig:94 | `coshf 3.000000: 10.067663` -> `coshf 3.000000: 10.067662` | `40400000` -> `3.000000`; `41211525` -> `10.067662` |
| trig:97 | `coshf -3.000000: 10.067663` -> `coshf -3.000000: 10.067662` | `C0400000` -> `-3.000000`; `41211525` -> `10.067662` |
| trig:99 | `sinhf 5.000000: 74.203208` -> `sinhf 5.000000: 74.203209` | `40A00000` -> `5.000000`; `4294680B` -> `74.203209` |
| trig:102 | `sinhf -5.000000: -74.203208` -> `sinhf -5.000000: -74.203209` | `C0A00000` -> `-5.000000`; `C294680B` -> `-74.203209` |
| ttrig:56 | `acos -0.250000: 1.823476` -> `acos -0.250000: 1.823477` | `BE800000` -> `-0.250000`; `3FE967AE` -> `1.823477` |
| ttrig:75 | `value 1.500000. sinf 0.997498, xsinf 0.997463` -> `value 1.500000. sinf 0.997498, xsinf 0.997464` | `3FC00000` -> `1.500000`; `3F7F5C04` -> `0.997498`; `3F7F59C5` -> `0.997464` |
| ttrig:78 | `value -1.500000. sinf -0.997498, xsinf -0.997463` -> `value -1.500000. sinf -0.997498, xsinf -0.997464` | `BFC00000` -> `-1.500000`; `BF7F5C04` -> `-0.997498`; `BF7F59C5` -> `-0.997464` |

#### Every approved performance baseline update

**524 existing rows change; `tfpround` is the only new row.**
Each was remeasured with normal PowerShell stack checking, both modes.
`-UpdatePerfBaseline` selected only candidates whose linked metrics changed
with the first exact-decimal alternative, plus the changed-stdout apps and
new `tfpround`. The final smaller/faster sequence left two provisional size
candidates (`tfloat4`, `tmathf`) unchanged; their checked rows remain identical. The checked
suite deliberately defaults `dcc_floatio=true` even for integer-only programs
(`Get-DccFloatio`), so formatter dependencies affect their linked sizes too:
24 rows change cycles, 523 change size. Apps with the explicit
floatio-off override and no float formatting stay unchanged. **Maintainer
approval covers these runtime-driven stdout/performance updates.**

`=` means that column is exactly unchanged; all changed cells show old -> new.
COM sizes are emulator-checked file sizes (128-byte quantized). No existing
peep or nopeep cycle count increases; no existing image grows. Sizes decrease
by up to 2176 bytes. The 20 unchanged original rows remain byte-identical.

| App | Peep cycles | Nopeep cycles | Peep bytes | Nopeep bytes |
| --- | ---: | ---: | ---: | ---: |
| 00040b | = | = | 6272 -> 4224 | 6272 -> 4224 |
| catalan | = | = | 8448 -> 6400 | 8576 -> 6400 |
| e | = | = | 5248 -> 3200 | 5376 -> 3200 |
| fact | = | = | 5376 -> 3328 | 5376 -> 3328 |
| fcall1 | = | = | 5376 -> 3328 | 5504 -> 3328 |
| fileops | = | = | 12032 -> 9984 | 12160 -> 10112 |
| ln2 | = | = | 7168 -> 4992 | 7168 -> 5120 |
| mm | 125251357 -> 125184909 | 126675973 -> 126609525 | 7936 -> 7168 | 7936 -> 7296 |
| nqueens | = | = | 5632 -> 3584 | 5632 -> 3584 |
| pihex | 689144476 -> 689090020 | 691517464 -> 691463008 | 11136 -> 11008 | 11392 -> 11264 |
| primes | = | = | 6272 -> 4224 | 6272 -> 4224 |
| sieve | 16857093 -> 16840203 | 17302294 -> 17284054 | 5120 -> 3072 | 5120 -> 3072 |
| t | = | = | 12032 -> 9856 | 12416 -> 10368 |
| t2darr | = | = | 6528 -> 4480 | 6656 -> 4608 |
| t2denum | = | = | 5120 -> 2944 | 5120 -> 3072 |
| tabort | = | = | 9216 -> 7040 | 9216 -> 7040 |
| tabsidm | = | = | 5504 -> 3328 | 5504 -> 3328 |
| taddr | = | = | 5504 -> 3456 | 5504 -> 3456 |
| taggordr | = | = | 5504 -> 3328 | 5632 -> 3584 |
| tallocx | = | = | 15488 -> 13440 | 16000 -> 13952 |
| tandself | = | = | 5376 -> 3328 | 5376 -> 3328 |
| taninit | = | = | 6144 -> 4096 | 6144 -> 4096 |
| tanonagg | = | = | 5888 -> 3840 | 5888 -> 3840 |
| tap | 167807559 -> 167801334 | 168343823 -> 168337598 | 7296 -> 6272 | 7296 -> 6400 |
| tappend | = | = | 9600 -> 7424 | 9600 -> 7552 |
| targlim | = | = | 7424 -> 5248 | 7936 -> 5888 |
| targs | = | = | 6144 -> 4096 | 6272 -> 4224 |
| tarray | = | = | 8320 -> 6272 | 8448 -> 6400 |
| tarray6 | = | = | 7552 -> 5504 | 7680 -> 5504 |
| tarresc | = | = | 5632 -> 3584 | 5632 -> 3584 |
| tarrtd | = | = | 6272 -> 4096 | 6272 -> 4224 |
| tasinfsp | = | = | 7680 -> 7424 | 7680 -> 7424 |
| tasm | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tasmcoll | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tasmdrop | = | = | 5376 -> 3200 | 5376 -> 3328 |
| tassert | = | = | 5248 -> 3072 | 5248 -> 3200 |
| tatan2sp | = | = | 8704 -> 8448 | 8704 -> 8448 |
| tatexit | = | = | 5376 -> 3200 | 5376 -> 3200 |
| tatof | = | = | 9216 -> 9088 | 9216 -> 9088 |
| tautolcs | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tb6exp | = | = | 6912 -> 6784 | 6912 -> 6784 |
| tb6pow | = | = | 10112 -> 9984 | 10112 -> 9984 |
| tbcgcol | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tbcint | = | = | 5120 -> 2944 | 5120 -> 2944 |
| tbcloop | = | = | 6144 -> 4096 | 6144 -> 4096 |
| tbcreg | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tbcregno | = | = | 5248 -> 3072 | 5248 -> 3072 |
| tbcreld | = | = | 5120 -> 2944 | 5120 -> 2944 |
| tbcsar | = | = | 5760 -> 3712 | 5888 -> 3712 |
| tbcshl | = | = | 5760 -> 3712 | 5888 -> 3840 |
| tbdos | = | = | 5760 -> 3712 | 5760 -> 3712 |
| tbfcnt | = | = | 5504 -> 3456 | 5632 -> 3584 |
| tbfcomp | = | = | 8192 -> 6144 | 8448 -> 6272 |
| tbfdate | = | = | 5504 -> 3456 | 5632 -> 3584 |
| tbfexpr | = | = | 6784 -> 4736 | 7296 -> 5248 |
| tbfinit | = | = | 5888 -> 3712 | 5888 -> 3712 |
| tbfprot | = | = | 5504 -> 3456 | 5632 -> 3584 |
| tbig | = | = | 13568 -> 11392 | 13824 -> 11776 |
| tbigfunc | = | = | 29696 -> 27648 | 37248 -> 35200 |
| tbios | = | = | 5376 -> 3328 | 5376 -> 3328 |
| tbiosrg | = | = | 6272 -> 4096 | 6400 -> 4352 |
| tbitfld | = | = | 6784 -> 4736 | 7040 -> 4992 |
| tbits | = | = | 7424 -> 5376 | 7552 -> 5504 |
| tbits32 | = | = | 6016 -> 3840 | 6016 -> 3840 |
| tblkabi | = | = | 5632 -> 3584 | 5760 -> 3712 |
| tblkcopy | = | = | 5760 -> 3584 | 5760 -> 3584 |
| tblkdiv | = | = | 6272 -> 4096 | 6272 -> 4224 |
| tblkneg | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tblkret | = | = | 5632 -> 3584 | 5760 -> 3712 |
| tblktern | = | = | 8576 -> 6528 | 8704 -> 6656 |
| tblktype | = | = | 7040 -> 4992 | 7168 -> 5120 |
| tbool | = | = | 11136 -> 9088 | 12032 -> 9984 |
| tboolbf | = | = | 5888 -> 3712 | 5888 -> 3840 |
| tboolbr | = | = | 5760 -> 3712 | 5888 -> 3840 |
| tbrelide | = | = | 5504 -> 3456 | 5888 -> 3712 |
| tbsearch | = | = | 7936 -> 5888 | 8064 -> 6016 |
| tbufex | = | = | 5632 -> 3456 | 5632 -> 3456 |
| tbug | = | = | 5760 -> 3712 | 5888 -> 3712 |
| tbug2 | = | = | 5504 -> 3328 | 5504 -> 3328 |
| tbuiltof | = | = | 5376 -> 3200 | 5504 -> 3328 |
| tbyteeq | = | = | 6528 -> 4480 | 6528 -> 4480 |
| tbytephi | = | = | 5504 -> 3456 | 5632 -> 3456 |
| tbytepre | = | = | 14336 -> 12288 | 19072 -> 17024 |
| tc89bit | = | = | 5504 -> 3456 | 5504 -> 3456 |
| tc89comp | = | = | 7040 -> 4992 | 7168 -> 5120 |
| tc89core | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tc89decl | = | = | 5760 -> 3712 | 5760 -> 3712 |
| tc89fadd | = | = | 5760 -> 3712 | 5760 -> 3712 |
| tc89fcmp | = | = | 6144 -> 4096 | 6144 -> 4096 |
| tc89fcnv | = | = | 5632 -> 3712 | 5760 -> 3712 |
| tc89fdiv | = | = | 6016 -> 4480 | 6016 -> 4480 |
| tc89ffio | 118738 -> 59254 | 118768 -> 59284 | 5120 -> 4096 | 5120 -> 4096 |
| tc89fini | = | = | 5760 -> 3712 | 5760 -> 3712 |
| tc89flng | = | = | 5504 -> 3328 | 5504 -> 3328 |
| tc89flt | = | = | 5376 -> 3200 | 5376 -> 3200 |
| tc89flta | = | = | 6784 -> 4736 | 6784 -> 4736 |
| tc89fltc | = | = | 5760 -> 3584 | 5760 -> 3712 |
| tc89fmat | = | = | 6656 -> 6400 | 6656 -> 6400 |
| tc89fmul | = | = | 5760 -> 5120 | 5760 -> 5120 |
| tc89fnty | = | = | 5632 -> 3584 | 5632 -> 3584 |
| tc89fp | = | = | 5632 -> 3584 | 5632 -> 3584 |
| tc89fptr | = | = | 5632 -> 3456 | 5632 -> 3456 |
| tc89fs | = | = | 5632 -> 3584 | 5632 -> 3584 |
| tc89ini2 | = | = | 5760 -> 3712 | 6016 -> 3968 |
| tc89init | = | = | 6144 -> 4096 | 6144 -> 4096 |
| tc89pp | = | = | 5248 -> 3200 | 5376 -> 3200 |
| tc89ptr | = | = | 5888 -> 3840 | 5888 -> 3840 |
| tc89qual | = | = | 5504 -> 3456 | 5504 -> 3456 |
| tc89size | = | = | 10368 -> 8320 | 10496 -> 8320 |
| tc89swjt | = | = | 6656 -> 4608 | 6784 -> 4736 |
| tc89uac | = | = | 5632 -> 3584 | 5632 -> 3584 |
| tc99apar | = | = | 5632 -> 3584 | 5632 -> 3584 |
| tc99ctl | = | = | 6016 -> 3968 | 6144 -> 4096 |
| tc99init | = | = | 6784 -> 4736 | 6912 -> 4864 |
| tc99scpe | = | = | 6528 -> 4352 | 6656 -> 4480 |
| tc99varm | = | = | 5760 -> 3584 | 5760 -> 3584 |
| tcaidxb | = | = | 5376 -> 3200 | 5376 -> 3328 |
| tcalb11 | = | = | 15360 -> 13312 | 16000 -> 13952 |
| tcalsuf | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tcanest | = | = | 5248 -> 3200 | 5376 -> 3200 |
| tcapmbr | = | = | 5376 -> 3328 | 5504 -> 3328 |
| tcaslv | = | = | 11392 -> 10368 | 11776 -> 10752 |
| tcastasg | = | = | 5632 -> 3584 | 5760 -> 3712 |
| tcastcnd | = | = | 7168 -> 4992 | 7296 -> 5120 |
| tcastlog | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tcharbit | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tchess | = | = | 18048 -> 16000 | 19712 -> 17536 |
| tchptr | = | = | 5248 -> 3072 | 5248 -> 3200 |
| tclit | = | = | 7680 -> 6656 | 7680 -> 6656 |
| tcmbyte | = | = | 6016 -> 4992 | 6272 -> 5376 |
| tcmdapp | = | = | 6656 -> 4608 | 7040 -> 4992 |
| tcmp | = | = | 9472 -> 8448 | 9600 -> 8576 |
| tcmpabi | = | = | 5888 -> 3840 | 6144 -> 3968 |
| tcmpq | = | = | 8704 -> 7680 | 8704 -> 7680 |
| tcmprhs | = | = | 8448 -> 8320 | 8448 -> 8320 |
| tcmpval | = | = | 8960 -> 6912 | 9216 -> 7168 |
| tcmt99 | = | = | 5760 -> 3712 | 5760 -> 3712 |
| tcmwide | = | = | 8704 -> 6656 | 9344 -> 7168 |
| tcnstfld | = | = | 5760 -> 3712 | 5760 -> 3712 |
| tcodegen | = | = | 6912 -> 4736 | 7040 -> 4992 |
| tconstdz | = | = | 5376 -> 3328 | 5376 -> 3328 |
| tconstp | = | = | 5504 -> 3456 | 5504 -> 3456 |
| tconstun | = | = | 5504 -> 3456 | 5632 -> 3584 |
| tconv10 | = | = | 9344 -> 7296 | 9728 -> 7680 |
| tcpirlp | = | = | 5248 -> 3072 | 5248 -> 3072 |
| tcptrarr | = | = | 5632 -> 3456 | 5632 -> 3584 |
| tcrcfix | = | = | 10112 -> 7936 | 10368 -> 8320 |
| tcsecall | = | = | 5504 -> 3456 | 5504 -> 3456 |
| tctresc | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tctrlz | = | = | 10496 -> 8320 | 10496 -> 8448 |
| tctrreg | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tctxflt | = | = | 17792 -> 17664 | = |
| tctxops | = | = | 10496 -> 10368 | 10496 -> 10368 |
| tctype | = | = | 7168 -> 5120 | 7168 -> 5120 |
| tctypw | = | = | 5888 -> 3840 | 5888 -> 3840 |
| tdblnot | = | = | 6400 -> 4352 | 6656 -> 4608 |
| tdblnotw | = | = | 5760 -> 3712 | 5888 -> 3840 |
| tdblptr | = | = | 5632 -> 3456 | 5632 -> 3584 |
| tdblptw | = | = | 5632 -> 3584 | 5760 -> 3584 |
| tdead | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tdeadblk | = | = | 5888 -> 3840 | 6272 -> 4224 |
| tdecinit | = | = | 5504 -> 3456 | 5504 -> 3456 |
| tdecl | = | = | 6912 -> 4864 | 7040 -> 4992 |
| tderefpi | = | = | 5376 -> 3328 | 5376 -> 3328 |
| tdirpat | = | = | 8448 -> 6400 | 8448 -> 6400 |
| tdivcmp | = | = | 7168 -> 4992 | 7168 -> 5120 |
| tdivmod | = | = | 6528 -> 4480 | 6528 -> 4480 |
| tdivzero | = | = | 9728 -> 7552 | 9728 -> 7680 |
| tdmfuse | = | = | 8320 -> 6272 | 8448 -> 6400 |
| tdowhile | = | = | 5376 -> 3328 | 5376 -> 3328 |
| tdrive | = | = | 9344 -> 7296 | 9344 -> 7296 |
| tearly16 | = | = | 5760 -> 3712 | 5888 -> 3840 |
| tecreg | = | = | 5120 -> 2944 | 5120 -> 3072 |
| tenum | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tenumfsm | = | = | 5120 -> 3072 | 5120 -> 3072 |
| terrno | = | = | 12288 -> 10240 | 12416 -> 10368 |
| terrwid | = | = | 6016 -> 3840 | 6016 -> 3968 |
| tesc | = | = | 11776 -> 9600 | 11776 -> 9728 |
| texargs | = | = | 12160 -> 10112 | 13312 -> 11264 |
| texbnd08 | = | = | 8320 -> 6144 | 8448 -> 6400 |
| texec | = | = | 7168 -> 5120 | 7296 -> 5248 |
| texecdp | = | = | 7040 -> 4992 | 7040 -> 4992 |
| texfile | = | = | 8576 -> 6400 | 8576 -> 6400 |
| texlog | = | = | 7168 -> 4992 | 7168 -> 5120 |
| texpfsp | = | = | 7040 -> 6912 | 7040 -> 6912 |
| texscan | = | = | 7936 -> 5888 | 7936 -> 5888 |
| texsort | = | = | 4992 -> 2944 | 4992 -> 2944 |
| texstrct | = | = | 5760 -> 3712 | 5760 -> 3712 |
| tfaedge | 756108 -> 422799 | 756155 -> 422846 | 5632 -> 4608 | 5632 -> 4608 |
| tfarrsub | = | = | 7040 -> 4864 | 7168 -> 4992 |
| tfcarg2d | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tfchunk | = | = | 9472 -> 7424 | 9600 -> 7424 |
| tfdedge | 537355 -> 407930 | 537402 -> 407977 | 5376 -> 3840 | 5376 -> 3840 |
| tfdf | = | = | 6912 -> 5760 | 6912 -> 5760 |
| tfdivr | = | = | 6016 -> 4480 | 6016 -> 4480 |
| tfedge | = | = | 9984 -> 7936 | 10112 -> 7936 |
| tfeof | = | = | 10880 -> 8832 | 11008 -> 8960 |
| tfileapp | = | = | 11008 -> 8960 | 11264 -> 9216 |
| tfilero | = | = | 10624 -> 8448 | 10752 -> 8704 |
| tfio | = | = | 8960 -> 6912 | 8960 -> 6912 |
| tfiob5 | = | = | 12928 -> 10880 | 13440 -> 11392 |
| tfixarr | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tflcini | = | = | 6016 -> 5760 | 6016 -> 5888 |
| tflcnot | = | = | 6016 -> 5760 | 6144 -> 6016 |
| tflddecl | = | = | 5888 -> 3712 | 6144 -> 4096 |
| tfldparr | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tflidx | = | = | 9472 -> 8704 | 10112 -> 9216 |
| tflinc | = | = | 6272 -> 5248 | 6528 -> 5504 |
| tflmadd | = | = | 7808 -> 7680 | 7808 -> 7680 |
| tflncmp | = | = | 7936 -> 7808 | 7936 -> 7808 |
| tflnot | = | = | 5632 -> 4992 | 5760 -> 4992 |
| tfloorsp | = | = | 7808 -> 6784 | 7808 -> 6784 |
| tflscast | = | = | 5760 -> 3712 | 5760 -> 3712 |
| tfltret | = | = | 5760 -> 5248 | 5760 -> 5248 |
| tflucast | = | = | 5632 -> 5376 | 5632 -> 5504 |
| tfmadd | 388547 -> 235368 | 389060 -> 235881 | 6272 -> 5504 | 6400 -> 5504 |
| tfmaddr | = | = | 6784 -> 5888 | 6784 -> 5888 |
| tfmaf | = | = | 7552 -> 6656 | 7552 -> 6656 |
| tfmedge | 402603 -> 301509 | 402650 -> 301556 | 5376 -> 4608 | 5376 -> 4608 |
| tfmode | = | = | 10368 -> 8320 | 10624 -> 8576 |
| tfmodfsp | = | = | 9984 -> 9728 | 9984 -> 9856 |
| tfncond | = | = | 5376 -> 3328 | 5376 -> 3328 |
| tfnxtcvt | = | = | 8320 -> 6400 | 8448 -> 6400 |
| tfo | = | = | 8704 -> 6656 | 8704 -> 6656 |
| tfopenw | = | = | 8832 -> 6784 | 8832 -> 6784 |
| tforblk | = | = | 6912 -> 4864 | 7040 -> 4864 |
| tforcomm | = | = | 5760 -> 3584 | 5760 -> 3712 |
| tfordia | = | = | 5376 -> 3328 | 5376 -> 3328 |
| tforfrm | = | = | 5120 -> 3072 | 5248 -> 3072 |
| tforhex | = | = | 5504 -> 3456 | 5632 -> 3584 |
| tforhome | = | = | 7040 -> 4992 | 7424 -> 5248 |
| tforinc | = | = | 5504 -> 3328 | 5504 -> 3456 |
| tforjmp | = | = | 5504 -> 3328 | 5632 -> 3456 |
| tforpred | = | = | 5248 -> 3072 | 5248 -> 3072 |
| tforsco | = | = | 8320 -> 6272 | 8448 -> 6400 |
| tfpaext | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tfpcall | = | = | 5888 -> 3712 | 5888 -> 3712 |
| tfpos | = | = | 9344 -> 7168 | 9344 -> 7296 |
| tfpraw | = | = | 7296 -> 5376 | 7296 -> 5376 |
| tfpshad | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tfpspec | = | = | 12672 -> 12416 | 12800 -> 12544 |
| tfracphi | = | = | 6144 -> 4096 | 6272 -> 4096 |
| tfreid | = | = | 8704 -> 6656 | 8832 -> 6656 |
| tfreopen | = | = | 9088 -> 7040 | 9088 -> 7040 |
| tfret | = | = | 5120 -> 4480 | 5120 -> 4480 |
| tfrexpsp | = | = | 7040 -> 4992 | 7040 -> 4992 |
| tfrndop | = | = | 10624 -> 10368 | 10624 -> 10368 |
| tfrxb6 | = | = | 9856 -> 9728 | 9984 -> 9728 |
| tfseekr | = | = | 9984 -> 7936 | 9984 -> 7936 |
| tfsubadd | = | = | 7040 -> 6016 | 7040 -> 6016 |
| tfuncid | = | = | 5504 -> 3456 | 5632 -> 3456 |
| tfwdtag | = | = | 5248 -> 3200 | 5376 -> 3200 |
| tgbcmp | = | = | 5760 -> 3712 | 5760 -> 3712 |
| tgcdphi | = | = | 5760 -> 3712 | 5760 -> 3712 |
| tginaddr | = | = | 5504 -> 3456 | 5504 -> 3456 |
| tginitad | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tginitu | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tginoffs | = | = | 5376 -> 3328 | 5504 -> 3456 |
| tgnarly | = | = | 7936 -> 5888 | 8448 -> 6400 |
| tgoto | = | = | 5888 -> 3712 | 5888 -> 3840 |
| tgotocap | = | = | 5248 -> 3072 | 5248 -> 3072 |
| thoistbc | = | = | 5632 -> 3584 | 5760 -> 3584 |
| thyp10 | = | = | 7936 -> 7808 | 8064 -> 7936 |
| thypb6 | = | = | 7808 -> 7680 | 7808 -> 7680 |
| tidxasgn | = | = | 5376 -> 3200 | 5376 -> 3200 |
| tidxrmw | = | = | 5760 -> 3712 | 5760 -> 3712 |
| tifcom | = | = | 4992 -> 2944 | 4992 -> 2944 |
| tinc32a | = | = | 6144 -> 3968 | 6400 -> 4224 |
| tindopt | = | = | 5248 -> 3200 | 5376 -> 3328 |
| tindres | = | = | 5248 -> 3200 | 5376 -> 3328 |
| tinitbf | = | = | 5888 -> 3712 | 5888 -> 3840 |
| tinitbr | = | = | 5376 -> 3328 | 5504 -> 3328 |
| tinitcs | = | = | 5376 -> 3328 | 5376 -> 3328 |
| tinitfc | = | = | 5504 -> 3456 | 5504 -> 3456 |
| tinitreg | = | = | 6912 -> 4864 | 6912 -> 4864 |
| tinittyp | = | = | 7424 -> 5376 | 7552 -> 5504 |
| tinline | = | = | 6656 -> 4608 | 7040 -> 4864 |
| tinlinfb | = | = | 6144 -> 4096 | 6144 -> 4096 |
| tinlnpar | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tioerr | = | = | 9984 -> 7936 | 9984 -> 7936 |
| tisnan | = | = | 6400 -> 4224 | 6400 -> 4224 |
| tiyreg | = | = | 7040 -> 4864 | 7168 -> 5120 |
| tkandr | = | = | 5632 -> 3584 | 5760 -> 3584 |
| tkrfnptr | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tlbreuse | = | = | 5248 -> 3072 | 5248 -> 3072 |
| tlcont | = | = | 5760 -> 3584 | 5760 -> 3584 |
| tldref | = | = | 6144 -> 4096 | 6272 -> 4224 |
| tlimbsh | = | = | 9088 -> 7040 | 9600 -> 7424 |
| tlimits | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tlmod | = | = | 7424 -> 5376 | 7424 -> 5376 |
| tlmul | = | = | 6912 -> 4864 | 6912 -> 4864 |
| tlngcond | = | = | 5504 -> 3456 | 5504 -> 3456 |
| tlngfptr | = | = | 5632 -> 3456 | 5632 -> 3456 |
| tlngnarw | = | = | 6400 -> 4352 | 6528 -> 4480 |
| tlocalfp | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tlog | 1009961 -> 895822 | 1010018 -> 895879 | 7936 -> 7808 | 7936 -> 7808 |
| tlogblk | = | = | 6400 -> 4352 | 6656 -> 4480 |
| tlogfsp | = | = | 7552 -> 7296 | 7552 -> 7296 |
| tlogwide | = | = | 6656 -> 4736 | 6784 -> 4736 |
| tlong | = | = | 6656 -> 4480 | 6656 -> 4608 |
| tlongaud | = | = | 6912 -> 4864 | 7168 -> 5120 |
| tlongcnv | = | = | 7552 -> 5376 | 7552 -> 5504 |
| tlongfn | = | = | 9472 -> 7424 | 9472 -> 7424 |
| tlongidx | = | = | 5632 -> 3584 | 5632 -> 3584 |
| tlongopt | = | = | 19968 -> 17920 | 20864 -> 18816 |
| tlongreg | = | = | 8704 -> 6528 | 8832 -> 6656 |
| tlongsub | = | = | 7808 -> 5760 | 7808 -> 5760 |
| tloopdef | = | = | 5248 -> 3072 | 5248 -> 3200 |
| tlpcall | 556423 -> 453112 | 560949 -> 457638 | 8960 -> 8704 | 9344 -> 9088 |
| tm | = | = | 7168 -> 5120 | 7168 -> 5120 |
| tm1mu | = | = | 6144 -> 4096 | 6144 -> 4096 |
| tmacwide | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tmalloch | = | = | 6016 -> 3840 | 6016 -> 3968 |
| tmatbit | = | = | 6016 -> 3840 | 6016 -> 3968 |
| tmatha | = | = | 9984 -> 9856 | 9984 -> 9856 |
| tmdstr | = | = | 5248 -> 3200 | 5504 -> 3456 |
| tmemaddr | = | = | 5376 -> 3328 | 5504 -> 3456 |
| tmeminfo | = | = | 6784 -> 4608 | 7424 -> 5248 |
| tmemtm | = | = | 5376 -> 3200 | 5376 -> 3200 |
| tmerge16 | = | = | 6144 -> 4096 | 6400 -> 4352 |
| tmerrb6 | = | = | 13952 -> 13824 | 13952 -> 13824 |
| tmfcall | = | = | 5376 -> 3328 | 5376 -> 3328 |
| tmfstat | = | = | 5248 -> 3200 | 5376 -> 3200 |
| tmfstr | = | = | 5760 -> 3712 | 5888 -> 3840 |
| tmircfg | = | = | 5120 -> 2944 | 5120 -> 3072 |
| tmirconv | = | = | = | 13568 -> 13440 |
| tmirfast | = | = | 7936 -> 5760 | 8064 -> 5888 |
| tmirfix | = | = | 9216 -> 7168 | 10240 -> 8064 |
| tmirfuse | = | = | 9088 -> 7040 | 9344 -> 7296 |
| tmirhome | = | = | 6016 -> 3968 | 6144 -> 4096 |
| tmirlife | = | = | 9344 -> 7296 | 10112 -> 8192 |
| tmirload | = | = | 6272 -> 4224 | 6400 -> 4352 |
| tmirslot | = | = | 6144 -> 4096 | 6272 -> 4096 |
| tmod3216 | = | = | 5888 -> 3840 | 5888 -> 3840 |
| tmodp2 | = | = | 7936 -> 5888 | 8064 -> 6016 |
| tmuldiv | = | = | 8832 -> 6784 | 8832 -> 6784 |
| tmulpow2 | = | = | 5632 -> 3584 | 5632 -> 3584 |
| tnamecol | = | = | 5376 -> 3200 | 5504 -> 3456 |
| tnarrow | = | = | 5248 -> 3200 | 5376 -> 3200 |
| tnarwin | = | = | 4992 -> 2944 | 4992 -> 2944 |
| tnddecay | = | = | 6144 -> 4096 | 6144 -> 4096 |
| tndwide | = | = | 6016 -> 3968 | 6144 -> 4096 |
| tnegidx | = | = | 6656 -> 4608 | 6912 -> 4736 |
| tnestfor | = | = | 6400 -> 4352 | 6528 -> 4480 |
| tnestini | = | = | 5248 -> 3072 | 5248 -> 3200 |
| tnestpi | = | = | 6144 -> 3968 | 6144 -> 4096 |
| tnoretrn | = | = | 5248 -> 3072 | 5248 -> 3200 |
| toffset | = | = | 5504 -> 3328 | 5504 -> 3328 |
| too | = | = | 20480 -> 18432 | 21248 -> 19200 |
| tpadread | = | = | 9856 -> 7680 | 9856 -> 7808 |
| tparrgrp | = | = | 6400 -> 4224 | 6528 -> 4352 |
| tpeepal | = | = | 5376 -> 3328 | 5504 -> 3328 |
| tpeepi | = | = | 5376 -> 3328 | 5376 -> 3328 |
| tpeepop | = | = | 5504 -> 3328 | 5504 -> 3456 |
| tpeepsum | = | = | 6784 -> 4736 | 7040 -> 4992 |
| tpeeq8 | = | = | 5632 -> 3456 | 5632 -> 3584 |
| tpeerng | = | = | 7168 -> 5120 | 7680 -> 5504 |
| tpeesc | = | = | 6272 -> 5248 | 6272 -> 5248 |
| tpephl | = | = | 5632 -> 3456 | 5888 -> 3712 |
| tperror | = | = | 5376 -> 3328 | 5376 -> 3328 |
| tpfargs | 682088 -> 658171 | 692570 -> 668653 | 8576 -> 6528 | 8704 -> 6528 |
| tpfauto | 312246 -> 264322 | 312273 -> 264349 | 5888 -> 3840 | 5888 -> 3840 |
| tpfbuf | 1480566 -> 1309280 | 1706969 -> 1535683 | 12160 -> 10112 | 12544 -> 10496 |
| tpffmt | 720894 -> 694302 | 780121 -> 753529 | 6912 -> 4736 | 7040 -> 4864 |
| tpfinf | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tpfio | 390059 -> 241813 | 390686 -> 242440 | 8448 -> 7424 | 8576 -> 7552 |
| tpflio | 540231 -> 450123 | 540858 -> 450750 | 8448 -> 6272 | 8448 -> 6400 |
| tphi | 1058546 -> 994721 | 1058573 -> 994748 | 5376 -> 3840 | 5376 -> 3840 |
| tphiarg | = | = | 5248 -> 3072 | 5248 -> 3200 |
| tphiarm | = | = | 5632 -> 3584 | 5632 -> 3584 |
| tphijoin | = | = | 6144 -> 4096 | 6400 -> 4352 |
| tphislot | = | = | 8448 -> 7424 | 8960 -> 7936 |
| tpi | = | = | 6400 -> 4352 | 6400 -> 4352 |
| tpihexb | = | = | 6016 -> 3968 | 6016 -> 3968 |
| tplng | = | = | 8320 -> 6272 | 8448 -> 6400 |
| tponce | = | = | 5376 -> 3328 | 5376 -> 3328 |
| tporthdr | = | = | 5248 -> 3200 | 5376 -> 3200 |
| tportio | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tpost16 | = | = | 5632 -> 3584 | 5632 -> 3584 |
| tpost32 | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tpost8 | = | = | 5888 -> 3840 | 6144 -> 4096 |
| tpostai | = | = | 5376 -> 3200 | 5376 -> 3200 |
| tpostbr | = | = | 5888 -> 3840 | 6016 -> 3968 |
| tpostfld | = | = | 5632 -> 3456 | 5632 -> 3584 |
| tpostidx | = | = | 4992 -> 2944 | 4992 -> 2944 |
| tpostinc | = | = | 6144 -> 3968 | 6144 -> 4096 |
| tpostptr | = | = | 11392 -> 9344 | 11904 -> 9856 |
| tpostut | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tpowfsp | = | = | 8576 -> 8448 | 8576 -> 8448 |
| tppasgn | = | = | 5248 -> 3200 | 5376 -> 3200 |
| tppreg | = | = | 5376 -> 3328 | 5504 -> 3328 |
| tppsuf | = | = | 5248 -> 4352 | 5248 -> 4352 |
| tpragstk | = | = | 5248 -> 3072 | 5248 -> 3072 |
| tpreinc | = | = | 6144 -> 4096 | 6144 -> 4096 |
| tpreproc | = | = | 6144 -> 4096 | 6144 -> 4096 |
| tprintf | 1070346 -> 986209 | 1070373 -> 986236 | 7040 -> 4992 | 7040 -> 4992 |
| tpromo | = | = | 9216 -> 8448 | 9344 -> 8576 |
| tpromo2 | = | = | 6528 -> 4352 | 6528 -> 4480 |
| tpromo32 | = | = | 6912 -> 4864 | 6912 -> 4864 |
| tptarith | = | = | 5376 -> 3328 | 5376 -> 3328 |
| tptr2dv | = | = | 5376 -> 3328 | 5376 -> 3328 |
| tptrarr | = | = | 5760 -> 3712 | 5888 -> 3712 |
| tptrcnd | = | = | 17536 -> 15360 | 18944 -> 16896 |
| tptrcst | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tptrdiff | = | = | 5376 -> 3328 | 5504 -> 3328 |
| tptrinit | = | = | 6144 -> 3968 | 6144 -> 4096 |
| tptrixld | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tptrlhs | = | = | 16768 -> 14720 | 18048 -> 15872 |
| tptrmac | = | = | 5248 -> 3072 | 5248 -> 3200 |
| tptrrhs | = | = | 27264 -> 25088 | 31104 -> 28928 |
| tqsort | 66477980 -> 66477917 | = | 9984 -> 7936 | 10624 -> 8448 |
| trealloc | = | = | 6656 -> 4608 | 6784 -> 4608 |
| treg | = | = | 7040 -> 4864 | 7040 -> 4992 |
| tregnarw | = | = | 5120 -> 3072 | 5248 -> 3072 |
| tregwide | = | = | 5376 -> 3328 | 5504 -> 3328 |
| trenamex | = | = | 9344 -> 7296 | 9344 -> 7296 |
| trenwild | = | = | 8320 -> 6272 | 8448 -> 6272 |
| tret | = | = | 6912 -> 4864 | 6912 -> 4864 |
| tretnar | = | = | 5376 -> 3328 | 5376 -> 3328 |
| triangle | = | = | 5120 -> 3072 | 5120 -> 3072 |
| trig | 12793704 -> 10109185 | 12794063 -> 10109544 | 11648 -> 11520 | 11648 -> 11520 |
| trnb5 | = | = | 8576 -> 6528 | 8704 -> 6656 |
| trowcmp | = | = | 5376 -> 3200 | 5376 -> 3328 |
| trowinv | = | = | 5120 -> 3072 | 5120 -> 3072 |
| trowptr | = | = | 5248 -> 3200 | 5248 -> 3200 |
| trtl2 | = | = | 9856 -> 7808 | 9984 -> 7936 |
| trw | = | = | 9984 -> 7936 | 10112 -> 7936 |
| trw2 | = | = | 10496 -> 8448 | 10752 -> 8704 |
| trwold | = | = | 9216 -> 7168 | 9216 -> 7168 |
| ts | = | = | 6272 -> 4224 | 6272 -> 4224 |
| ts32 | = | = | 6912 -> 4864 | 6912 -> 4864 |
| tscandir | = | = | 13696 -> 11520 | 13696 -> 11648 |
| tscanf | = | = | 12416 -> 10368 | 12544 -> 10368 |
| tscanin | = | = | 7936 -> 5888 | 7936 -> 5888 |
| tselfadj | = | = | 6016 -> 3968 | 6016 -> 3968 |
| tselfop | = | = | 6912 -> 6784 | 7040 -> 6912 |
| tsetjiy | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tsetjmp | = | = | 5248 -> 3200 | 5376 -> 3200 |
| tshlmac | = | = | 5632 -> 3712 | 5760 -> 3712 |
| tsizetyp | = | = | 5120 -> 3072 | 5248 -> 3072 |
| tsjdeep | = | = | 5632 -> 3456 | 5632 -> 3584 |
| tsnprtf | = | = | 5888 -> 3840 | 5888 -> 3840 |
| tsparse | = | = | 9984 -> 7936 | 9984 -> 7936 |
| tsprintf | = | = | 6912 -> 4864 | 6912 -> 4864 |
| tsqrtsp | = | = | 7040 -> 6784 | 7040 -> 6784 |
| tsretmem | = | = | 5632 -> 3584 | 5632 -> 3584 |
| tsretret | = | = | 5888 -> 3840 | 6016 -> 3968 |
| tstackov | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tstar | = | = | 7936 -> 5888 | 7936 -> 5888 |
| tstchain | = | = | 5504 -> 3328 | 5632 -> 3456 |
| tstdc | = | = | 5248 -> 3072 | 5248 -> 3072 |
| tstdlib | = | = | 9472 -> 7424 | 9600 -> 7552 |
| tstfield | = | = | 6144 -> 4096 | 6272 -> 4096 |
| tstr | 1343091356 -> 1343090549 | 1368265056 -> 1368264412 | 11136 -> 9088 | 11520 -> 9472 |
| tstr2 | = | = | 6272 -> 4224 | 6400 -> 4352 |
| tstr3 | = | = | 7552 -> 5504 | 7680 -> 5504 |
| tstraddr | = | = | 5120 -> 3072 | 5248 -> 3072 |
| tstrb11 | = | = | 9728 -> 9600 | 9984 -> 9856 |
| tstrcmpf | = | = | 6272 -> 4224 | 6272 -> 4224 |
| tstrcmpi | = | = | 5504 -> 3328 | 5504 -> 3328 |
| tstrconv | = | = | 12416 -> 10240 | 12544 -> 10368 |
| tstretst | = | = | 6912 -> 4864 | 7040 -> 4864 |
| tstrftm | = | = | 9728 -> 7680 | 10112 -> 8064 |
| tstri2 | = | = | 5376 -> 3200 | 5376 -> 3200 |
| tstrify | = | = | 5632 -> 3584 | 5632 -> 3584 |
| tstring | = | = | 9216 -> 7040 | 9088 -> 7040 |
| tstrnul | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tstruct | = | = | 6144 -> 4096 | 6144 -> 4096 |
| tstructi | = | = | 5632 -> 3584 | 5632 -> 3584 |
| tstructp | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tstructv | = | = | 9344 -> 7296 | 9472 -> 7424 |
| tsvbmod | = | = | 5632 -> 3584 | 5760 -> 3712 |
| tsvbuf2 | = | = | 10624 -> 8576 | 10624 -> 8576 |
| tswitch | = | = | 5248 -> 3072 | 5248 -> 3072 |
| tswneg | = | = | 5248 -> 3072 | 5248 -> 3200 |
| tsyntax | = | = | 6528 -> 4480 | 6528 -> 4480 |
| tszlogic | = | = | 5120 -> 3072 | 5248 -> 3200 |
| tszswap | 99486414 -> 99477864 | 133734086 -> 133727516 | 6144 -> 4096 | 6528 -> 4480 |
| ttdstr | = | = | 5632 -> 3456 | 5888 -> 3840 |
| tternarg | = | = | 6656 -> 4608 | 6912 -> 4864 |
| ttmp | = | = | 9472 -> 7424 | 9472 -> 7424 |
| ttmpb7 | = | = | 7168 -> 5120 | 7168 -> 5120 |
| ttmpcl | = | = | 10496 -> 8320 | 10496 -> 8448 |
| ttrg6sp | = | = | 10368 -> 10112 | 10368 -> 10240 |
| ttrig | 40303224 -> 38212327 | 40353951 -> 38263054 | = | = |
| ttt | = | = | 6144 -> 4096 | 6272 -> 4096 |
| ttype2 | = | = | 5888 -> 3840 | 6016 -> 3968 |
| ttype32 | = | = | 5888 -> 3840 | 5888 -> 3840 |
| ttypesr | = | = | 5888 -> 3840 | 5888 -> 3840 |
| tugcore | = | = | 11136 -> 9088 | 11136 -> 9088 |
| tugpath | = | = | 12928 -> 10880 | 13056 -> 10880 |
| tunary | = | = | 6400 -> 4352 | 7424 -> 5376 |
| tunary32 | = | = | 6016 -> 3968 | 6016 -> 3968 |
| tunaryp | = | = | 7296 -> 5120 | 7296 -> 5248 |
| tunbf | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tunfphi | = | = | 5504 -> 3456 | 5632 -> 3584 |
| tungetc | = | = | 9088 -> 6912 | 9088 -> 7040 |
| tunion | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tunion2 | = | = | 6144 -> 4096 | 6272 -> 4224 |
| tunmsg1 | = | = | 5376 -> 3328 | 5504 -> 3328 |
| tunmsg2 | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tunused | = | = | 5248 -> 3200 | 5248 -> 3200 |
| tupdprov | = | = | 5504 -> 3328 | 5504 -> 3328 |
| tvapinit | = | = | 5376 -> 3328 | 5376 -> 3328 |
| tvariad | = | = | 6016 -> 3968 | 6144 -> 4096 |
| tvla | = | = | 24192 -> 22144 | 29312 -> 27264 |
| tvlaero | = | = | 5888 -> 3840 | 6144 -> 4096 |
| tvlaisle | = | = | 6912 -> 4736 | 7168 -> 4992 |
| tvlapal | = | = | 5760 -> 3584 | 5888 -> 3840 |
| tvlaparm | = | = | 7168 -> 5120 | 7296 -> 5248 |
| tvlapk | = | = | 6016 -> 3840 | 6144 -> 4096 |
| tvlasub | = | = | 5632 -> 3584 | 5760 -> 3712 |
| tvlax | = | = | 7040 -> 4864 | 7168 -> 5120 |
| tvolopt | = | = | 5888 -> 3712 | 5888 -> 3712 |
| tvpapar | = | = | 5120 -> 3072 | 5120 -> 3072 |
| tvplain | = | = | 7680 -> 5632 | 7808 -> 5760 |
| twcasgm | = | = | 6528 -> 4480 | 6656 -> 4608 |
| twhcomma | = | = | 8320 -> 7424 | 8448 -> 7552 |
| twild | = | = | 8320 -> 6144 | 8320 -> 6272 |
| twincadd | = | = | 7296 -> 5248 | 7552 -> 5376 |
| twincg | = | = | 6528 -> 4480 | 6656 -> 4608 |
| twphicpy | = | = | 7296 -> 5248 | 7552 -> 5376 |
| tzpad | = | = | 8448 -> 6400 | 8448 -> 6400 |
| wumpus | = | = | 14592 -> 12544 | 15104 -> 13056 |

New measured row `tfpround`: peep_cycles=627367, nopeep_cycles=627384, peep_size=3840, nopeep_size=3840.


#### Release gates and limitations

- Canonical all-tool build passes; no compiler/debug-metadata implementation changes.
- Strict PowerShell full+extended **with stack checking**: 584 apps discovered,
  560 passed, 24 standard override skips, zero failed; perf, diagnostics,
  dccpeep fixtures and extended suite pass.
- Strict PowerShell full+extended **without stack checking**: the same 560
  passed/24 standard skips/zero failed; diagnostics, fixtures and extended pass.
- Normal-stack remeasurement of runtime-affected candidates: 527 passed,
  zero failed; 524 existing rows change, 20 remain byte-identical, one new row.
- `validate-unit-test.ps1 -App tfpround`: host O0/O2, sanitizers and target
  proof pass; normal peep/nopeep perf row is checked.
- Both MIR censuses: all 3672 unchanged-source functions byte-identical.
- IY-safety and API/public-label coverage audits pass; strict MkDocs passes.
- After promotion, known-bugs runner: seven fail, zero unexpectedly FIXED.
- `clang-oracle-fuzz.py --gen float --seeds 1-300 --configs production
  --off known --timeout 6 --jobs 6`: **286 ok / 14 INVALID / zero FAIL**.
  No generator features changed; exact decimal rendering is proven by the
  dedicated linked table rather than this raw-bit arithmetic fuzzer.
- Full Python discovery: **338 tests**, failures=3, errors=10, standard skips=2.
  Two assertion failures are the pre-answered **#277** modules
  `test_mir_campaign_baselines` and `test_mir_exact_controls`; neither is fixed,
  refreshed or skipped. The remaining failure/errors were supervisor/mutation
  harness timing (two extra errors were timeout-following fixture cleanup).
  With every other F1 long job finished, one serial isolated runner retried
  all **nine** affected test methods: **seven pass**, while two pre-existing
  harness tests still fail: `CompilerMutationTests.test_child_timeout_and_environment_isolation`
  (the 2-second child deadline loses its expected pre-timeout diagnostic) and
  `CompilerMutationTests.test_default_serial_and_parallel_are_equal_and_bounded`
  (the synthetic scheduler exceeds its 180-second subprocess deadline).
  Both use unchanged PowerShell/fake-compiler fixtures, not the changed float
  runtime. No harness is fixed or skipped; no old compiler/main checkout is
  run, as pre-answered. **The full Python gate is not green**: those two timing
  checks remain unverified, alongside the acknowledged #277 assertions.
  Local logs: `build/f1-python-tests.log` and `build/f1-python-isolated.log`.
- New bugs: none. Exhaustive enumeration of all 2^32 bit patterns is not
  claimed; precision proof samples are explicit above. `%e`/`%g` and existing
  upstream arithmetic/libm approximations are deliberately not fixed.
- Original filtered census SHA256: no-stack
  `46bbc2b260ba6c3f682439f4d725fa87a59f8d0e7eb546ebd37c0bd6ecea0101`;
  stack `f86f1d9bc90c9e0fa1ca3bc2665c2a70e0791316d5cca0b03c15357992e030a9`.
  Both filtered post-fix snapshots have exactly these hashes.

## Tracking

| Cluster | Branch / PR | Status | Notes |
| --- | --- | --- | --- |
| F1 | [#330](https://github.com/davidly/dcc/pull/330) (`fix/cluster-f1-printf-rounding`) | PR open; runtime/release proof complete; known Python exceptions documented | BUG-060 promoted as tfpround; maintainer-approved 16 stdout lines/524 perf rows, no cycle increase or image growth; formatter -2 bytes; both 3672-function censuses identical |
| N | [#312](https://github.com/davidly/dcc/pull/312) (`fix/cluster-n-float-array-else`) | PR open; release gates verified | BUG-055 promoted as tflidx; restored float 283 ok/17 INVALID/zero FAIL; both 3,574-function censuses and all 530 existing perf rows unchanged; #277 plus isolated harness retries documented |
| A1 | [#281](https://github.com/davidly/dcc/pull/281) | merged | 007/025/031 promoted; 300 seeds clean; existing output/baselines unchanged; approved pre-existing Python failures #277/#280 |
| A2 | `fix/cluster-a2-block-typing` (no PR) | stopped; split | First root fix cleared only 008; 004/015 still failed; experiment discarded |
| A2a | [#283](https://github.com/davidly/dcc/pull/283) | PR open | BUG-008 promoted as `tblkneg`; 300 seeds clean; existing output/baselines unchanged; Python failures match clean main (#277) |
| A2b | [#314](https://github.com/davidly/dcc/pull/314) (`fix/cluster-a2b-block-typing`) | split after first root fix | A2b-1 clears original 004, not 015; restored fuzzing finds 004 byte SSA facet (A2b-3); neither issue closes; D2d provenance is a sibling, not the same fix |
| A2b-1 | [#314](https://github.com/davidly/dcc/pull/314) (`fix/cluster-a2b-block-typing`) | PR open; verified compiler gates; partial issues | Canonical repaired assignment reuses boolean-source widening contract; all original 004 lines pass; new host-proven tlogblk covers cleared facets, no partial reproducer promoted |
| A2b-2 | | split; BUG-015 remains open | Pointer integer ordering cleared in A4; bit-field facet already cleared by E2-bitfield #310; casts and float consumers remain independently owned |
| A2b-3 | [#329](https://github.com/davidly/dcc/pull/329) (`fix/cluster-a2b3-logical-byte`) | PR open; compiler gates verified; Python exceptions documented | BUG-004 promoted as tlogbyte; byte logical PHI retains its source type; restored integer 283 ok/14 INVALID/3 limit-only/zero FAIL; both 3,672-function existing censuses and all 544 existing perf rows unchanged |
| A4 | [#328](https://github.com/davidly/dcc/pull/328) (`fix/cluster-a4-block-pointer-typing`) | PR open; verified partial BUG-015 fix | Printed lines 152/153/154/162/163 flip; 155 already passes at base; tptrblk added; 3,672 existing functions and 544 perf rows unchanged; steered fuzz 283 ok/14 INVALID/3 limits; restored seed 96 exposes store-conversion remainder; harness retries documented; no steering/reproducer removal |
| A2c | [#318](https://github.com/davidly/dcc/pull/318) (`fix/cluster-a2c-block-type-resolution`) | draft PR; stopped; root trial withdrawn | Early lexical typing clears trial BUG-015 facets but changes 259/3,613 functions per mode and existing checked performance; BUG-004 byte PHI is independent; docs only, no steers/promotions |
| A3 | [#331](https://github.com/davidly/dcc/pull/331) (`fix/cluster-a3`) | merged; BUG-058 and BUG-015 closed | BUG-058 promoted as tblkword, BUG-015 as tblkbind (with A4 + cherry-picked D2e-2 repair); tblkcast kept; all existing census functions and perf rows unchanged; five steers removed; restored integer 293 ok/3 INVALID/3 limit/1 FAIL (seed 261 → BUG-064), float 295 ok/5 INVALID; seed 96 → BUG-063 with `blockstore` steer |
| T1 | [#334](https://github.com/davidly/dcc/pull/334) (`fix/cluster-t1-promoted-slot-hole`) | merged; BUG-064 closed | hole proof rejects fused self-store/PHI-homed promoted locals; BUG-064 promoted as tslothol; all 591 test outputs byte-identical, census and 545 perf rows unchanged |
| S2 | `fix/bug062-sizeof-size_t` | BUG-062, BUG-065 and BUG-066 closed; BUG-067 and BUG-068 logged | full unsigned `sizeof` with AST proven-signedness demotion; emitter/dccpeep recovery, 0 perf regressions / 255 improvements; tsizesgn, tvaiyhom and tszblock promoted; 39 campaign hashes refreshed; generator emits `sizeof` |
| T2 | [#335](https://github.com/davidly/dcc/pull/335) (`fix/cluster-t2-store-conversion`) | merged; BUG-063 closed | implicit indirect-store conversion retargeted by pointee repair; BUG-063 promoted as tblkstor; `blockstore` steer removed; all test outputs byte-identical, 546 perf rows unchanged; int 1–300: 296 ok/4 INVALID/0 FAIL |
| T3 | `fix/bug067-068-block-deferred` | Merged (#342); BUG-067 and BUG-068 closed; BUG-069 logged | deferred `sizeof` and `INDEXADDR` typing for nested-block derived operands; tszderiv and tptrcasg promoted; five VLA exact-schedule hashes and the packed-record control hashes refreshed (main's were wrong); 0 perf regressions / 146 improvements; `blockcassign` steer removed |
| T4 | `fix/bug069-member-sizeof` | Merged (#343); BUG-069 closed | `sizeof` of a whole or partially indexed multi-dimensional array member uses the field size / remaining-dimension product; tszmdim promoted; 0 perf regressions / 146 improvements |
| T5 | `fix/proof-suite-gates` | PR open; closes #277 and #280 | compound-check-runner exact schedule fingerprint refreshed after the bool-branch rewrite (cmp15ok/cmp4ok/cmp45ok controls pass again; wave-45 audit baseline and indices updated); process supervisor prefers `setsid`, then a direct perl setsid launcher, over a per-launch pwsh supervisor (removes the macOS exit-139 flake); 0 perf regressions |
| B | `fix/cluster-b-stack-homes` (no PR) | stopped; split | First root fix cleared only 030; 014/019/026/036 unchanged; experiment rejected for six existing-function changes |
| B1 | [#286](https://github.com/davidly/dcc/pull/286) | merged | BUG-030 promoted as tforhome; 300 seeds clean; 500 existing perf rows and 3,366 existing census functions unchanged; strict stack/no-stack pass; approved #277 failures only (#280 fixture passes) |
| B2 | `fix/cluster-b2-home-fatals` | stopped; split | First root fix clears only 014; separate traces disprove shared cause |
| B2a | [#292](https://github.com/davidly/dcc/pull/292) | PR open | BUG-014 promoted as twincg; 300 integer seeds clean; 3,426 census functions and 507 perf rows unchanged; approved #277 failures only (#280 passes) |
| B2b | [#303](https://github.com/davidly/dcc/pull/303) (`fix/cluster-b2b-dead-decl`) | PR open | BUG-019 promoted as tdeadblk; 300 integer/float seeds clean; 3,489 existing functions and 517 perf rows unchanged; BUG-022 reproducer-only flip, diagnostic mask remains open; 13/14 isolated Ninja harness tests pass; one mock serial-scheduler timeout remains unverified |
| B2c | [#311](https://github.com/davidly/dcc/pull/311) | fixed in M-a | BUG-057/#304 shares M-a direct-location home fix; promoted as twincadd; no extra compiler change |
| B3 | [#289](https://github.com/davidly/dcc/pull/289) | merged | BUG-026/017 same root; tselfop/tandself promoted; int 213 ok/85 INVALID/2 limit-only, float 267 ok/33 INVALID; zero FAIL; 504 existing perf rows and 3,394 census functions unchanged; new BUG-054/#288 parked separately |
| B4 | [#293](https://github.com/davidly/dcc/pull/293) | PR open | BUG-036 promoted as tlpcall; C/D independent; both 300-seed batches clean; 3,436 existing functions and 509 perf rows unchanged |
| C1 | [#313](https://github.com/davidly/dcc/pull/313) (`fix/cluster-c-local-merge`) | PR open; compiler gates verified; accepted Python harness exceptions | BUG-027/#247 and BUG-038/#258: materialize word PHI edges for store-only fused adjustments; original 3,558 functions/mode and 528 perf rows unchanged |
| C2 | | closed | BUG-029/037/052 traced at the C1 handoff and fixed in C2a/C2b/C2c; distinct from BUG-057/#304 |
| C2c | [#317](https://github.com/davidly/dcc/pull/317) (`fix/cluster-c2c-slot-coalesce`) | PR open; compiler gates verified; accepted Python harness exceptions | BUG-052/#272 promoted as tphislot; float 291 ok/9 INVALID/zero FAIL; 3,603 existing functions/mode and 534 existing perf rows unchanged |
| C2b | [#319](https://github.com/davidly/dcc/pull/319) (`fix/cluster-c2b-wide-phi-edge`) | PR open; compiler gates verified; approved Python harness exceptions | BUG-037/#257 promoted as twphicpy; shared earlier-PHI edge-use proof; restored integer 280 ok/19 INVALID/1 limit-only/zero FAIL; 3,603 existing functions/mode and 534 perf rows unchanged; new BUG-058/#315 parked/steered |
| C2a | [#320](https://github.com/davidly/dcc/pull/320) (`fix/cluster-c2a-phi-liveness`) | PR open; compiler gates verified; accepted Python harness exceptions | BUG-029/#249 promoted as tphiarm; restored bytearm 293 ok/7 INVALID/zero FAIL; both 3,603-function censuses and all 534 existing perf rows unchanged; #277 plus one isolated mock serial-scheduler timeout unverified |
| D | | split after first root fix | D1 fixes typed update arithmetic (003/044); D2 contains independent value-lifetime/admission defects (021/023/024/010/041); traces above |
| D1 | [#297](https://github.com/davidly/dcc/pull/297) (`fix/cluster-d-postinc`) | PR open; verified compiler gates | 003/044 promoted as tinc32a/tflinc; int 223 ok/77 INVALID, float 286 ok/14 INVALID, zero FAIL; 3,461 existing functions byte-identical in both censuses and 510 existing perf rows unchanged; only BUG-042 steering refined outside cluster; strict release gates pass; approved #277 plus one isolated mutation-harness process failure documented |
| D2 | | split into D2a / D2b / D2c / D2d / D2e | Assignment-value lifetime, AST admission, byte-PHI lifetime and float consumer typing are independent |
| D2a | [#300](https://github.com/davidly/dcc/pull/300) (`fix/cluster-d2a-postinc-home`) | merged | 021/023 promoted as tpost16/tpostbr; 023 moved from C, same root; 024 remains independent (D2c); 3,487 pre-follow-up functions and 517 perf rows unchanged |
| D2b | [#301](https://github.com/davidly/dcc/pull/301) (`fix/cluster-d2b-postinc-admit`) | merged | 010 promoted as tpost32; 041 admission withdrawn after new BUG-056/#299; int 227/73, blocked float 281/19, zero FAIL; 3,482 existing functions and 516 perf rows byte-identical; strict release gates pass; #277 plus five isolated harness exceptions documented |
| D2c | [#305](https://github.com/davidly/dcc/pull/305) (`fix/cluster-d2c-byte-phi-home`) | PR open; verified compiler gates | 024 promoted as tpost8; 266 ok/34 INVALID/zero FAIL; 3,503 existing functions and 519 perf rows unchanged; 24 parked reproducers all fail; approved #277 and four isolated harness failures documented |
| D2d | [#307](https://github.com/davidly/dcc/pull/307) (`fix/cluster-d2d-float-incdec-value`) | draft PR; stopped; admission withdrawn | 041/056 trial FIXED; dependent block-local float update arithmetic/comparison still wrong; restored float 288 ok/11 INVALID/1 FAIL (224); full trial withdrawn; final 20 reproducers: pre-existing 022 FIXED, 19 fails; both 3,532-function censuses and all 525 existing perf rows unchanged |
| D2e | [#321](https://github.com/davidly/dcc/pull/321) (`fix/cluster-d2e-incdec-provenance`) | split under stop rule | D2e-1 retains AST update provenance only; D2e-2 float admission/MIR trial clears 041/056 but restored seed 224 exposes missing implicit operand conversion (new BUG-059/#316) |
| D2e-1 | [#321](https://github.com/davidly/dcc/pull/321) (`fix/cluster-d2e-incdec-provenance`) | verified compiler gates; partial scope | Safe update provenance only; tupdprov added; 3,613 existing functions/mode and 535 perf rows unchanged; final float 283/17 and int 293/7, zero FAIL; strict 551/24 gates; #277 untouched, isolated fixture retry passes |
| D2e-2 | [#337](https://github.com/davidly/dcc/pull/337) (`fix/cluster-d2e2-admission`) | merged; BUG-041, BUG-056, BUG-059 closed | float update value admitted via `ast_update_lvalue_float_type()`; regional compaction made debug-marker transparent for `-gline` identity; promoted as tfincval/tfnegupd/tfupdcnv (+tblfcons); all outputs byte-identical, 546 perf rows unchanged; int 1–300 296 ok/4 INVALID/0 FAIL; float 1–300 297 ok / 3 INVALID / 0 FAIL. Earlier: root repair shipped via #331; `mir_repair_deferred_float_types()` and tflbconv landed by cherry-pick (f311a376); 041/056/059 remain steered by fincdecval and admission-blocked; agent stopped on quota before float update admission |
| E | [#306](https://github.com/davidly/dcc/pull/306) (`fix/cluster-e-signedness`) | split into E1 / E2 | Single SSA forwarding fix clears 016; 004 function facet and 015 word-widening facet improve, complete issues still open |
| E1 | [#306](https://github.com/davidly/dcc/pull/306) (`fix/cluster-e-signedness`) | PR open; verified compiler gates; documented harness exceptions | 016 promoted as tinittyp; int 297/2/1 limit, float 281/19, zero FAIL; 3,509 functions and 520 perf rows unchanged; #277 and three isolated synthetic-fixture timing failures |
| E2 | | split; byte-shift and bit-field bugs fixed | 013 fixed in E2-bitfield; 006 repaired in E2c with the approved tcmwide exception; 002/028 fixed in E2-shift; 004 repaired boolean conversion remains in A2b |
| E2-shift | [#309](https://github.com/davidly/dcc/pull/309) (`fix/cluster-e2-byte-shift`) | PR open; verified compiler gates; one harness exception | 002/028 share one promotion/narrowing root; tbcshl/tbcsar promoted; 297 ok/1 INVALID/2 limits/zero FAIL; both 3,545-function censuses and 526 existing perf rows unchanged; #277 plus one isolated mock serial/parallel timeout unverified |
| E2-bitfield | [#310](https://github.com/davidly/dcc/pull/310) (`fix/cluster-e2-bitfield`) | PR open; 013 accepted; 006 moved to E2c | 013 promoted as tbfexpr; 300 integer seeds clean; 3,545 existing functions and 526 performance rows unchanged; 006 originally stopped; maintainer-approved output exception is now handled in E2c |
| E2c | [#324](https://github.com/davidly/dcc/pull/324) (`fix/cluster-e2c-bitfield-compound-load`) | fixed; release/fuzz gates verified; harness retries documented | BUG-006 promoted as tbfcomp; maintainer-approved tcmwide change; refined high-bit-sensitive repair; no exact-to-spilled regressions |
| F | [#296](https://github.com/davidly/dcc/pull/296) | PR open | 011/018/043 promoted; single AST admission fix; 3,436 existing functions and 509 perf rows unchanged; int 213/85/2 limits, float 256/44, zero FAIL; independent BUG-055/#294 parked and steered |
| G | [#295](https://github.com/davidly/dcc/pull/295) | split; G1/G2 fixed | G1 clears 012; approved G2 fixes 033/040/042 independently |
| G1 | [#295](https://github.com/davidly/dcc/pull/295) | PR open | 012 promoted as tconstun; 216 ok/82 INVALID/2 limit-only; 509 perf rows and both censuses unchanged; approved #277 failures; unchanged mock fixture passes Ninja retry; no MIR/emitter changes |
| G2 | `fix/cluster-g2-const-fold-rest` | fixed; three independent commits | 033/040/042 promoted as tconstdz/tflcini/tflcnot; 900 programs: 806 ok/94 INVALID/zero FAIL; 511 original perf rows and both 3,465-function censuses unchanged |
| H | [#308](https://github.com/davidly/dcc/pull/308) (`fix/cluster-h-float-cmp`) | PR open; verified compiler gates | 020/045 promoted as tcmprhs/tflncmp; int 252/48 and float 283/17, zero FAIL; 3,514 original functions in both modes and 523 perf rows unchanged; BUG-057/#304 parked, not fixed; #277 and isolated mutation timing exceptions documented |
| I1 | [#285](https://github.com/davidly/dcc/pull/285) | merged | BUG-046 promoted as tfltret; 300 float seeds clean; existing assembly/perf rows unchanged |
| I2 | [#287](https://github.com/davidly/dcc/pull/287) | PR open | 048/050 promoted as tflucast/tflscast; 300 float seeds clean (267 ok, 33 INVALID); existing assembly/perf unchanged |
| J | [#290](https://github.com/davidly/dcc/pull/290) | merged; partial fix | 047 promoted as tflmadd; 300 float seeds: 269 ok, 31 INVALID; existing output unchanged; 049 fixed in J2, 039 subsequently fixed in R1 |
| J2 | [#291](https://github.com/davidly/dcc/pull/291) | PR open; partial fix | 049 promoted as tflnot; 300 float seeds 269 ok/31 INVALID/zero FAIL; only three one-instruction census changes and two approved perf rows; 039 reverted because all five complete numeric baselines fail host-clang proof |
| R1 | [#325](https://github.com/davidly/dcc/pull/325) | PR open; verified approved numeric/perf updates | 039 dual ABI/IY 4404 pairs exact in both modes, helper -11 bytes; restored float seeds 283 ok/17 INVALID/zero FAIL; maintainer approved 9 stdout lines, 5 test constants and 100 existing perf rows; only tstrb11:main/ttrg6sp:main move in existing censuses; both strict full+extended gates pass 551 apps, zero failures; Python #277 only after successful timing retry; new BUG-060 #322 / BUG-061 #323 logged |
| S1 | [#326](https://github.com/davidly/dcc/pull/326) (`fix/cluster-s1-sizeof-unsigned`) | compiler gates verified; Python exceptions documented; full typing deferred | BUG-061 promoted as tsizhigh; shared helper unsigned only for known sizes >= 0x8000; both 3672-function regular censuses and 548 extended rows unchanged; 544 perf rows unchanged plus one new row; residual BUG-062 #327 parked with no generator steer; full 173-function/36-exact recovery deferred |
| K | [#278](https://github.com/davidly/dcc/pull/278) | merged | Four approved independent rules; promoted tests and new performance rows; existing baselines unchanged |
| K2 | [#298](https://github.com/davidly/dcc/pull/298) | PR open | BUG-054 promoted as tpephl; 228 ok/72 INVALID/zero FAIL; all 3,047 MAC files per stack mode and 514 existing perf rows unchanged (except clock literal); #277 and isolated harness retries documented |
| L | [#279](https://github.com/davidly/dcc/pull/279) | PR open | BUG-051 promoted as `tppsuf`; existing baselines and corpus assembly unchanged |
| M | [#311](https://github.com/davidly/dcc/pull/311) | fixed; main merged | 009/057 share direct-location fix (tndwide/twincadd); 022 uses fresh aggregate va_arg diagnostic fixture + tlbreuse; restored batch 290 ok/9 INVALID/1 limit-only/zero FAIL; merged existing census/perf unchanged |

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
     right. Run `python scripts/run-dccpeep-tests.py`.
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
   `python scripts/validate-unit-test.py -App <name>`.
5. Run `python3 -m unittest discover -s scripts/tests -p 'test_*.py'`. Do
   NOT run `scripts/prepush.py`; it takes five to six hours and is the
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
