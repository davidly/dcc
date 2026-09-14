# Compiler source coverage

## Batched, parallel execution

Run focused new cases during development; collect the full corpus once for an
integrated checkpoint, not once per small mutation batch. The shipping compiler
still requires its independent strict release and performance checks before
publishing production changes. Test-only edits do not require rerunning
unchanged release/debugger gates.

`DCC_COVERAGE_JOBS` bounds the coverage build, main/extended runners, and host
CTest processes; it defaults to the online CPU count. Mutation audits and
debug censuses use the same value unless `DCC_COVERAGE_MUTATION_JOBS` or
`DCC_COVERAGE_CENSUS_JOBS` overrides them. The clobber runner defaults to eight
workers after a 4/8/12/16-worker instrumented benchmark found 8 fastest with
identical execution manifests. Override it with `DCC_COVERAGE_CLOBBER_JOBS`
after measuring a different host. Clobber workers are separate processes, so
diagnostic environment variables cannot race between tests.

The exhaustive mutation campaigns use a longest-first token scheduler. Four
workers are assigned to each ordinary campaign by default, while campaigns
with an enforced two-worker cap consume only two tokens. Completed campaigns
release their tokens immediately so queued work can use the full combined
budget instead of leaving a serial long tail. Override the ordinary campaign
size with `DCC_COVERAGE_CAMPAIGN_JOBS`. Each campaign receives its own `%8m`
LLVM profile pool so profile-file locking does not serialize otherwise
independent compiler processes. `DCC_COVERAGE_MUTATION_JOBS` is the combined
budget, not a per-campaign multiplier.

The one-command workflow remains the default. Alternatively split a checkpoint
into stages, always using the same absolute build directory and toolchain:

```sh
export DCC_COVERAGE_BUILD_DIR="$PWD/build/compiler-coverage"
export DCC_COVERAGE_JOBS="$(getconf _NPROCESSORS_ONLN)"
export DCC_COVERAGE_CLOBBER_JOBS=8
export DCC_COVERAGE_CAMPAIGN_JOBS=4
DCC_COVERAGE_STAGE=build sh scripts/compiler-coverage.sh
DCC_COVERAGE_STAGE=collect sh scripts/compiler-coverage.sh
DCC_COVERAGE_STAGE=report sh scripts/compiler-coverage.sh
```

`build` records input and executable hashes. `collect` verifies that identity,
starts a fresh profile pool, and records success only after every required
workload completes. `report` verifies input, tool, execution-manifest and
profile hashes without rebuilding or rerunning targets. A failed collection
cannot reuse a previous success stamp. Editing inputs requires a new build
checkpoint and collection; profiles from separate worker revisions or faulty
compiler mutants must not be combined. Initialized test-submodule files are
included in the input identity.

`inputs.json`, `build.json` and `collection.json` are generated provenance
artifacts, not coverage exclusions. A build-directory lock prevents overlapping
stages; separate checkpoints use separate directories.

The first integrated parallel checkpoint executed all 5,076 expected leaf
configurations (the preserved 5,052-case allocation batch plus 24 scanner
controls), with exact manifest equality and no removed cases. Report-only
regeneration preserved the collection stamp and profile hashes. Independent
compiler mutation measurements with two build jobs per worker were 335.09
seconds serial and 205.30 seconds with two workers; both baseline controls
passed and all nine mutants were killed with identical outcomes.

On the current 24-CPU host, the later 476-configuration `allocmut` benchmark
completed in 16.90, 14.51, 16.85, and 17.99 seconds with 4, 8, 12, and 16
workers respectively. All four manifests were identical. Eight workers are
therefore the measured default; adding workers beyond that increased CPU time
and wall time.

The same wave adds accepted/rejected sliding-maximum controls and fixes an
overflow in generic `MIR_VA_ARG` preflight: testing `offset > 126` avoids
overflowing `offset + 1` at `LONG_MAX`. Boundary assertions verify rejection
does not write output or consume labels and that retrying the valid candidate
without resetting state produces identical output.

The next two integrated waves execute 5,196 exact leaf configurations. They
add accepted and rejected controls for symbol-find, softmax, matrix-product-add
and directory-enumeration schedules, plus an overflow-safe transactional MIR
stream seek invariant. Two target-aware allocation matcher compiler mutants
raise the clean-build mutation set from nine to eleven. The execution manifest
contains 5,196 unique keys and exactly matches the independently enumerated
inventory.

The following integrated wave executes 5,292 exact leaf configurations and
adds matrix-product-store, symbol-insert, and compound-runner proof campaigns.
It also fixes malformed-MIR fallback transactions: nameless direct and
aggregate calls are now rejected before emitting text, recording externals, or
consuming labels, and repaired retries are byte-identical to clean controls.

The next integrated wave executes 5,420 exact leaf configurations. It restores
the aliased signed-byte sum schedule with complete alias, width, PHI and unary
proofs; hardens the constant do-while matcher; moves homed parameter validation
before candidate side effects; and fixes active scalar constant folding for
non-integer operands and `_Bool` normalization. The two new maintained helpers
are explicitly classified as active, while `ast_const_fold_strict` and the
legacy emitter path remain excluded and unexecuted.

The following wave executes 5,548 exact leaf configurations. It adds LCS and
packed-record proof campaigns, admits defined float assignments through
integer and `_Bool` pointer lvalues, and preflights the complete scalar-DAG
value graph before any output, external reference, or label side effect.
Review added a fastcall ABI rejection for the packed-record dump callback and
defined fractional `_Bool` conversion controls.

The next wave executes 5,648 exact leaf configurations and adds sliding-window
and ctype/realloc ABI campaigns. It also makes homed scalar-DAG rejection
transactional and keeps declaration placeholders and exclusive scope ends
aligned when deferred metadata inserts MIR instructions. The standalone
scalar-DAG host binary is part of both CTest and coverage provenance, and
isolated compiler-mutant workspaces copy every host C source.

The following wave executes 5,856 exact leaf configurations. It adds
multidimensional-array and recursive MinMax proof campaigns, rejects malformed
spill-slot operands before interval construction, and restores label state
after declined selector attempts. The selector isolation test links the normal
module object; an initial duplicate-translation-unit report was discarded and
the corrected collection contains no duplicate selector functions or coverage
exclusions.

The next wave executes 6,018 exact leaf configurations. It isolates the final
affine fallback, canonicalizes and preflights scalar-DAG casts, and adds
exhaustive byte-math plus fixture-backed directory-enumeration proof campaigns.
Review added direct-call requirements for byte-math helpers and complete fixed
and variadic argument ABI validation for every directory callback.

The following wave executes 6,224 exact leaf configurations. It adds numeric
bitfield assignments, required-operand validation for homed MIR, and further
packed-record ABI proofs. A fixed-softmax survivor audit reduced 206 accepted
semantic mutations to zero by proving operators, dataflow, local types, table
bounds, argument ABI, loops, and normalization; its upper-clamp oracle now uses
defined 16-bit arithmetic.

The next wave executes 6,320 exact leaf configurations. It adds numeric
assignments to byte/word structure members and a square-grid proof campaign,
requires opcode-specific operands before spilled allocation indexing, and
fully validates both current reordered endgame-scope opcode layouts. The
endgame controls use the shared clobber runner rather than a private harness.

The following wave executes 6,528 exact leaf configurations. It rejects
missing or unresolved spilled branch targets before frame planning and adds
additive-subscript, Fortran-fatal, and long-index proof campaigns. Static and
collision-mangled Fortran print/exit functions now use canonical assembler
names, and the long-index controls run through the shared inventory.

The next wave executes 6,602 exact leaf configurations. It validates homed
branch targets before planning, supports defined pointer compound assignments,
hardens abort-file runner ABI/dataflow, and fixes constant-function unsigned
comparisons. DCC's established target-width signed wrapping remains exact and
`tregnarw` retains its checked performance after a rejected over-conservative
overflow experiment.

The following wave executes 6,758 exact leaf configurations. It adds
transactional malformed direct/indirect AST-call controls, validates comparison
parameter displacements, and reduces a 127-mutation exec-recursion operator and
dataflow audit to zero survivors. All five LLVM report/export/show invocations
now include `mir-scalar-dag-test`; the binary was already instrumented and
provenance-tracked, but omitting it from the object list hid its host coverage.
The immutable collection contains 43 profile-pool files and its execution
manifest SHA-256 is
`eb7215b69af536402d4f09b358f3686329bfadaec7eb103b77ab832b206153c0`.

The next wave executes 6,854 exact leaf configurations. It adds live-result
multidimensional compound assignments, hardens compound-runner ABI/memory/CFG
proofs, and completes repeated-invariant-add validation. Review caught a
semantic `_Bool` counter acceptance; all boolean type surfaces now reject.
The repeated-add audit has zero survivors across 34 mutations. The compound
audit rejects 1,482/1,575 mutations and classifies the remaining 93 as 72
no-op field-identity rewrites, 10 byte-identical value-equivalent stores, and
11 runtime-proven overwritten-before-read stores. The immutable collection
contains 43 profile-pool files and manifest SHA-256
`d590eac25af8174ee7ec4f271fc51ee011db3c03c9fb0a4689f0254f7245ed96`.

The following integrated waves execute 7,436 exact leaf configurations and
add five linked host isolation binaries plus committed exhaustive mutation
campaigns for endgame scope, directory layouts, softmax, byte math, and
multidimensional arrays. They harden active AST-to-MIR lowering, deferred
metadata, homed/spilled CFG preflight, scalar DAGs, comparison branches,
constant evaluation, VLA smoothing, affine fill, call-safe member sums, and
the corresponding exact schedules. All correctness fixes preserve the checked
performance baselines.

The VLA host test originally textually included its production matcher module,
duplicating 272 maintained functions and producing an invalid 94.30% function
result. That report was discarded. The corrected test links the production
object once through a test-only entry point. The immutable corrected collection
contains 85 profile-pool files and manifest SHA-256
`afa127b37d9096d1a4e5233d81a0c540b04894638e1c3353c8fbeed8120e149a`.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,486 / 4,486 | 100.00% |
| Lines | 182,677 / 194,815 | 93.77% |
| Native branch outcomes | 95,622 / 149,058 | 64.15% |
| Regions | 165,083 / 175,115 | 94.27% |

The raw ledger has 53,192 uncovered outcomes. This is a checkpoint, not
completion of the broader four-metric objective.

Later correctness waves are locally validated through `1c15706c` and expand
the execution inventory to 9,564 unique leaves (SHA-256
`89c8702842240ff322a8948a84314ccdf17841de3cc97fbadc1be3d837bfbfad`).
Both strict release modes pass with zero regressions. These later inventories
do not replace the table above: a fresh immutable full collection is still
required before updating the authoritative four-metric totals.

The replacement Wave 30 LLVM 18 collection at
`864573c38db01a01f2b944d77a26f9132cfaacd2` is now the authoritative
checkpoint. Report-only regeneration revalidated the recorded source, tool,
binary, profile, and execution-manifest hashes without rerunning workloads.
The collection has 397 non-empty profile-pool files and exactly 9,564 unique
clobber executions; the manifest SHA-256 is
`89c8702842240ff322a8948a84314ccdf17841de3cc97fbadc1be3d837bfbfad`.
All maintained functions execute, but the remaining line, region, and branch
gaps still require semantic review:

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,608 / 4,608 | 100.00% |
| Lines | 190,711 / 202,512 | 94.17% |
| Native branch outcomes | 104,229 / 155,732 | 66.93% |
| Regions | 173,307 / 183,178 | 94.61% |

The raw ledger has 51,245 unreviewed uncovered branch outcomes and no
unexecuted maintained functions. This checkpoint supersedes the Wave 19
four-metric table for prioritization; it is not completion of the justified
coverage objective.

The first Wave 31 proof increment targets the recent full-debug metadata
repair rather than broad matcher short-circuit counts. Host invariants now
cover repeated call IDs, negative argument positions, short and excess fixed
arity, accepted variadic excess arguments, direct-call source operands,
non-function callees, release-mode non-repair, right-hand comparison repair,
and function-pointer calls that already have a callee value. Two clean-build
compiler mutants independently remove repeated-ID rejection and the full-debug
comparison gate; both are killed by their specific new invariant, raising the
compiler suite to 13/13 killed mutants.

Combining only the new instrumented host profile with the sealed Wave 30
profile shows the intended local effect without changing production source or
coverage denominators: `dcc_mir.c` gains 15 covered lines, 13 covered branch
outcomes, and three covered regions. The shared call validator rises from
40/50 to 48/50 covered branch outcomes, its function-pointer wrapper from
5/10 to 6/10, and `mir_resolve_deferred_metadata` from 698/880 to 702/880.
The follow-up controls cover negative call IDs, both prototype-count bounds,
unprototyped preservation, and accepted reverse physical order for unique
contiguous argument positions. The validator's only two remaining uncovered
outcomes are its defensive negative and past-end `call_index` checks; its sole
private caller passes the current instruction index from an in-range loop.
This focused overlay is gap-selection evidence, not a replacement full
checkpoint. All five normal and ASan/UBSan host tests and all 128 script tests
pass.

The first Wave 32 target-aware differential increment deliberately combines
contracts previously exercised separately. Its byte and word functions each
contain a two-input PHI, one aliasing call, maximum liveness seven, three
spills, four cross-call values, and two PHI moves before selecting
`spilled-scalar-cfg`. Eight independent target-width results cover both PHI
arms, aliasing and disjoint writes, and 8/16-bit wrap boundaries. A source
fault that flips the result bit causes all eight named oracle failures. The
data-only campaign adds 26 configurations, moving the exact clobber inventory
from 9,564 to 9,590; every stack/no-stack, peep/nopeep, full-debug, and
line-debug configuration passes.

A fresh current-tree instrumented compiler produced 27 non-empty profile
files for these 26 configurations. Overlaying them on Wave 30 for unchanged
production functions adds zero lines, branch outcomes, or regions: the full
corpus had already executed those generic-emitter outcomes independently.
That zero delta is retained as evidence rather than hidden. The increment's
value is the asserted interaction among PHI transfer, spills, call clobbers,
alias invalidation, and post-call reload. It does not replace the Wave 30
four-metric checkpoint or change its denominator. The independent oracle test
and all 129 repository script tests pass. Commit `ff638de3` contains the
fixture, campaign, and oracle.

Wave 33 adds 96 callable-PHI configurations, raising the exact inventory to
9,686. The campaign proves target-correct calls through conditional direct
function designators, local function pointers, explicit addresses, null and
cast-null alternatives, returned callables, compatible old-style/prototyped
functions, and nested returned-callable composites. The verifier's independent
MIR proof follows PHIs only when both source signatures match; incompatible or
partly unprototyped inputs remain unknown. Three clean-build mutants remove
PHI signature transport, conditional prototype recovery, and incompatible
conditional rejection; all three are killed, and the complete suite passes
one baseline plus 16/16 killed mutants.

The focused current-tree host and 96-case profiles cover
`mir_call_prototypes_match` at 23/23 regions, 15/15 lines, and 16/18 branch
outcomes. The two missing outcomes are bounds guards for malformed negative or
past-`MAX_PROTO_PARAMS` metadata, not supported-source paths. The broader value
resolver is 51/56 regions, 67/73 lines, and 30/42 branches; its remaining
outcomes include invalid value/definition, allocation-failure, cycle, and
unsupported-source defenses. These are focused current-tree measurements, not
an overlay claim against changed source mappings and not a replacement for
Wave 30. Commit `5d1bf884` contains the implementation and permanent controls.

Wave 34 records the AST-resolved scalar-call signature by call ID and makes
that owned snapshot authoritative during MIR verification. This covers casts,
fields, conditional values, and returned callables without requiring the
verifier to reverse-engineer every possible callee value graph. A present
unprototyped snapshot deliberately suppresses fallback name inference. The
existing 96 callable-PHI plus 12 `qualexpr` configurations all pass, so the
exact inventory remains 9,686.

Focused current-tree coverage reaches `mir_record_call_signature` at 21/23
regions, 34/36 lines, and 14/16 branch outcomes, and
`mir_resolve_call_prototype` at 26/27 regions, 39/40 lines, and 20/24 branch
outcomes. The complete compiler mutation suite has one passing baseline and
18/18 killed mutants; new independent mutations clear the stored signature
and remove the scalar lowering call site. Exact parent censuses have zero
stack or no-stack output changes. These measurements do not replace Wave 30,
and aggregate-call signature snapshots remain explicitly unresolved. Commit
`b4f870d8` contains the implementation and host proofs.

Wave 35 adds a mutation-specific allocation proof: a narrow SSA value live
across an ordinary call must receive callee-saved IY or a spill slot. Removing
the allocator's `cross_call` classification is killed by the host assertion,
while the existing `tmirslot.cross_call` target oracle passes peep/nopeep in
both stack modes. The complete compiler suite now has one passing baseline and
19/19 killed mutants. This is a test-only semantic invariant: the exact
inventory remains 9,686, production output is unchanged, and no additive raw
coverage is claimed. Commit `9cdf818e` contains the proof.

Wave 36 proves the corresponding wide-value rule with a non-rematerializable
derived `long` live across an ordinary direct call. Because neither HL:DE nor
BC:IY is wholly callee-saved, the wide-coloring probe must spill that value. A
clean-build mutant that incorrectly admits pair colors is killed by the exact
host assertion. Normal and ASan/UBSan host suites pass 5/5, the complete
compiler suite has one passing baseline and 20/20 killed mutants, strict
`tmirslot` passes both stack modes with no performance regression, and all 26
existing `phi-alias-wave32` target configurations pass. This remains a
test-only semantic proof: the inventory stays 9,686 and no raw coverage
increase is claimed. Commit `465ce55f` contains the proof.

Wave 37 proves the allocator/emitter contract behind the exceptional narrow
late-PHI path. A PHI physically after a direct void call may retain DE only
when the general stack-call path preserves that home. The host control checks
the DE allocation and ordered `push de`, call, and `pop de` output. A
clean-build mutant suppressing DE preservation is killed by that exact
assertion. Normal and ASan/UBSan host suites pass 5/5, the complete compiler
suite has one passing baseline and 21/21 killed mutants, strict `tmirlife`
passes both stack modes with no performance regression, and all 26 existing
`phi-alias-wave32` target configurations pass. This test-only semantic proof
leaves the exact inventory at 9,686 and makes no raw coverage claim. Commit
`ee97b296` contains the proof.

Wave 38 proves the corresponding wide exception through the production
regional homed candidate. A late `long` PHI physically crossing a direct void
call receives BC:IY, and the call is surrounded by ordered IY/BC saves and
BC/IY restores. A clean-build mutant that disables the unique general-call
BC:IY preservation computation is killed by that exact assertion. Normal and
ASan/UBSan host suites pass 5/5, the complete compiler suite has one passing
baseline and 22/22 killed mutants, strict `tmirlife` passes both stack modes
with no performance regression, and all 26 existing `phi-alias-wave32`
target configurations pass. This test-only semantic proof leaves the exact
inventory at 9,686 and makes no raw coverage claim. Commit `e65393ab` contains
the proof.

Wave 39 proves that isolated global-field value numbering invalidates the
def-use cache after replacing uses and turning a redundant load into a NOP.
The host graph primes the old value's cached definition and requires it to be
absent immediately after the pass, before a later verifier can mask the stale
entry by resetting caches. A clean-build mutant removes only that final
invalidation and is killed by the built-in cached-versus-uncached
`mir_definition` diagnostic. The mutation harness now distinguishes this
controlled cache-verifier failure from crashes and assertion kills. Normal and
ASan/UBSan host suites pass 5/5, all mutation-harness unit tests pass, the
complete compiler suite has one passing baseline and 23/23 killed mutants, and
72 generated fuzz target configurations pass with cache verification enabled.
This test-only semantic proof leaves the exact inventory at 9,686 and makes no
raw coverage claim. Commit `df67f458` contains the proof.

Wave 40 independently proves the regional paired-byte adjacency boundary. Its
compile-only probe requires the adjacent control to emit
`;@dcc.mir paired-byte-call` and the field-gap near match not to emit it. A
clean-build compiler mutant disables only the nonadjacent-offset rejection and
is killed by the exact near-match assertion; build failures, crashes, and
unrelated diagnostics remain invalid outcomes. The complete campaign has one
passing baseline plus 24/24 killed mutants. The target suite separately
executes the adjacent and field-gap forms in all stack/no-stack and peep/nopeep
modes, including forced regional fallback. This proof changes no production
code and makes no additive raw coverage claim.

Wave 41 adds a CFG near match to the same paired-byte source. The harmless
branch preserves target output but changes the MIR fingerprint. Normal
selection must use a named generic emitter and pass on target; explicitly
forcing `regional` must fail with both the intended `read_pair` validation
diagnostic and the unsafe-stream fatal. All five paired-byte controls pass in
stack/no-stack and peep/nopeep modes, for 20 executions. The frozen built-in
clobber inventory is 5,076 leaves. Production selection is unchanged, and the
latest exact coverage snapshot predates this test-only increment.

Wave 42 adds the missing symmetric independent-dominance control for a PHI
whose first logical predecessor has no incoming CFG edge. The test calls
`mir_verify_dominance` directly so the earlier structural verifier cannot mask
the independent check. Focused LLVM 18 coverage changes
`slot_predecessors[0] >= 0` from true-only to 50 true / 1 false. Normal and
ASan/UBSan host tests pass 5/5, all 134 script tests pass, and the complete
compiler campaign retains one passing baseline plus 24/24 killed mutants.
This test-only increment changes no production output; regenerate the exact
aggregate ledger before updating global totals.

Wave 43 closes the next `mir_value_number_global_field_loads` proof gap
without changing production code. The earlier host control already proved
positive redundant-load elimination; two new graphs now prove the missing
same-field-store and unsafe-call barriers. One keeps the first field load live
across a call-safe direct call, stores the same isolated static field, and
requires the later load not to fold to the stale value. The other seeds the
whole-file scan with two textual writes to the same field, inserts an
intervening call, and likewise requires no fold. Current code passes both
graphs, so this was a proof gap rather than a correctness defect. A new
clean-build mutant, `global-field-vn-call-barrier`, disables only the
non-call-safe call eviction and is killed by the exact
`FAIL isolated global field unsafe-call barrier` assertion. Normal and
ASan/UBSan host tests pass 5/5, the full compiler campaign has one passing
baseline plus 25/25 killed mutants, and all 136 Python script tests pass.
Commit `31c13e7c` contains the invariant and mutant.

Wave 44 closes the next `mir_try_emit_homed_scalar_cfg` proof gap without
changing production code. The existing homed transaction tests already built
nearby malformed `MIR_PARAM`, `MIR_LOAD_INDIRECT`, and `MIR_COPY_AGGREGATE`
graphs, but they did not prove the exact `mir_homed_reject` reasons that the
production generic fallback reports. New host controls now capture
`DCC_MIR_HOMED_REPORT` and require the precise `parameter-object`,
`parameter-type`, `indirect-load-type`, and `aggregate-copy-size` diagnostics,
each with preserved output prefixes and repaired same-stream retries that
match clean controls byte-for-byte. The clean-build mutant
`homed-aggregate-copy-size` disables only the aggregate size guard and is
killed by the exact `FAIL homed aggregate copy exact rejection` assertion.
Normal and ASan/UBSan host tests pass 5/5, the full compiler campaign has one
passing baseline plus 26/26 killed mutants, and all 136 Python script tests
pass. This remains a test-only increment; the authoritative aggregate totals
still await a fresh immutable collection.

Wave 45 closes the next `mir_match_compound_check_runner` proof gaps without
changing production code. The earlier compound coverage already proved the
exact baseline and selected ABI, memory-width, and final-tail mutations, but
it did not force generic fallback for five important near matches: a volatile
failure counter, an extra helper call, a harmless extra CFG block, a VLA
variant, and a fixed-prototype success printer. New `compound-wave45`
MIR-clobber cases add those controls plus four direct selector-mutant cases,
and the full group passes in 40 target configurations. A new
`compound-wave45-audit.py` campaign then runs 24 stack/no-stack and
peep/nopeep runtime controls and rejects 13/13 targeted MIR mutations that
cover store source-range validation, check-call identity, local-address and
pointer-load identity, indirect-load address/type, index-address source/type,
member offset/width, indirect-store value kind, and the final failure-load
type. Current code already rejected every new mutation and near match, so
this increment closes proof gaps rather than fixing a false acceptance. All
136 Python script tests pass. This remains a test-only increment; regenerate
the immutable aggregate ledger before claiming new overall branch totals.

Wave 46 closes the next `mir_match_symbol_insert_schedule` proof gaps without
changing production code. The earlier symbol-insert coverage already proved
the exact baseline, the wide field-mutation census, and selected symbol-limit
and field-offset near matches, but it did not keep a focused campaign over
return-shape drift, direct helper-signature mismatches, a non-canonical
memset call target, or several still-unproven argument/source identities.
New `symbol-insert-wave46` MIR-clobber cases add five source near matches and
eight direct selector-mutant cases, and the full group passes in 56 target
configurations. A new `symbol-insert-wave46-audit.py` campaign then runs 24
stack/no-stack and peep/nopeep runtime controls and rejects 8/8 targeted MIR
mutations that cover the error-call string argument, memset destination
argument, strncpy destination argument, the indexed-record count source, and
the kind/scope/size/element-size store sources. Current code already rejected
every new mutation and near match, so this increment closes proof gaps rather
than fixing a false acceptance. All 136 Python script tests pass. This
remains a test-only increment; regenerate the immutable aggregate ledger
before claiming new overall branch totals.

Wave 47 closes the next spilled generic-fallback proof gaps without changing
production code. Existing malformed-MIR transaction tests already exercised
nearby `MIR_LOAD_INDIRECT`, `MIR_CALL`, `MIR_CALL_AGGREGATE`, and
`MIR_VLA_SIZE` graphs, but they did not prove the exact
`mir_scalar_cfg_preflight_reject` reasons reported by spilled generic
preflight. New host controls now capture `DCC_MIR_SELECT_REPORT` and require
the precise `indirect-width`, `call-abi`, `aggregate-call-abi`, and
`frame-offset` diagnostics, each with preserved output prefixes,
empty-output rollback, and repaired same-stream retries that match clean
controls byte-for-byte. The clean-build mutant `spilled-call-abi` disables
only the empty-name branch of the direct-call ABI guard and is killed by the
exact `FAIL spilled call ABI exact rejection` assertion. Normal and
ASan/UBSan host tests pass 5/5, the full compiler campaign has one passing
baseline plus 27/27 killed mutants, and all 136 Python script tests pass.
This remains a test-only increment; the authoritative aggregate totals still
await a fresh immutable collection.

Wave 48 closes the next `ast_assign_supported_uncached` proof gaps without
changing production code. The existing host matrix already covered broad
identifier, indexed, pointer-array, multidimensional, and wide-scalar
assignment families, but it did not directly pin several member and
dereference classification branches in the remaining ledger. New direct-AST
controls in `tests/host/mir_verify.c` now assert support or rejection for:

- direct `.` and `->` pointer-member `+=`/`-=` compounds, plus rejection of a
  pointer rhs for those compounds;
- float-to-bitfield `=` conversion and bitfield `<<=` with a plain-int rhs;
- float-to-`_Bool` member `=` acceptance and `_Bool` member compound rejection;
- dead-result pointer-identifier `+=` acceptance with live-result rejection;
- dereferenced long `>>=` acceptance, dereferenced float `+=` acceptance, and
  dereferenced float `%=` rejection.

`tests/mir-clobber/assigncv.c` also adds a cheap end-to-end proof for local
pointer compounds, `box_pointer` member-pointer compounds, and bitfield
compound stores; manual `dccmake` peep/nopeep runs both report
`assignment coverage failures=0`. Normal and ASan/UBSan MIR host CTest pass
5/5, and all 136 Python script tests pass. No clean compiler mutant was
added: this predicate is a pure support classifier, and the newly covered
branches do not map cleanly onto an existing single-condition mutation with a
distinct downstream oracle. This remains a test-only increment; the
authoritative aggregate totals still await a fresh immutable collection.

Wave 49 closes the next `mir_match_ctype_realloc_schedule` proof gaps without
changing production code. The earlier ctype/realloc coverage already proved
the exact baseline, the exhaustive Wave 21 field-mutation census, the wave7
fastcall near matches, and selected ABI, width, string, and dataflow
mutations, but it did not keep a focused proof over fixed-prototype
allocation/grow/success printers, grow/shrink helper identity consistency,
variadic compare drift, late check-helper consistency, or several remaining
pointer-slot, argument-source, stride, and final-tail legality checks. New
`ctype-realloc-wave48` MIR-clobber cases add seven runtime controls plus five
runtime-safe selector-mutant controls, and all 48 target configurations pass.
A new `ctype-realloc-wave48-audit.py` campaign then runs 28 stack/no-stack and
peep/nopeep runtime controls and rejects 18/18 targeted MIR mutations that
cover pointer-store/load identity, allocation/grow/shrink null-test
operators, allocation/grow/final failure argument ordering, copy/preserve
dataflow, resize/check helper identity, byte-store and byte-check stride,
byte-check normalization, free-call argument indexing, and the final success
constant. Current code already rejected every new mutation and near match, so
this increment closes proof gaps rather than fixing a false acceptance. All
136 Python script tests pass. This remains a test-only increment; regenerate
the immutable aggregate ledger before claiming new overall branch totals.

Wave 50 closes the next `mir_match_vla_smooth` proof gaps without changing
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
PHI/branch plumbing, accumulation and increment links, average-store wiring,
alias-compare edges, and the return graph. Current code already rejected
every new mutation and accepted the boundary-layout control, so this
increment closes proof gaps rather than fixing a false acceptance. Normal and
ASan/UBSan MIR host CTest each pass `mir-vla-smooth-isolation`, and all 136
Python script tests pass. This remains a test-only increment; the
authoritative aggregate totals still await a fresh immutable collection.

Wave 51 closes the next `mir_resolve_deferred_metadata` proof gaps without
changing production code. Existing host deferred-metadata coverage already
proved function-pointer insertion, direct-call conversion repair, coordinate
updates, basic alias bounds, and malformed call ordering, but it did not
directly pin alias windows with explicit scope labels, for-init loop-exit
truncation, orphaned `MIR_OBJECT_MERGE` demotion, or the `#b`-gated scoped
unary/PHI type-repair loop. New direct MIR controls in
`tests/host/mir_verify.c` now assert scope-label alias renaming plus
`base_name` repair, non-repair when the scope label precedes the declaration
window, forward exit-branch truncation while ignoring a backward-targeted
`MIR_BRANCH_FALSE`, invalid array-object merge demotion to `MIR_ADDRESS`
while leaving a valid merge intact, and unary/PHI type repair after a block
alias retargets a named load. The clean-build mutant
`deferred-merge-demotion` disables only the invalid-merge fallback and is
killed by the exact `FAIL deferred metadata merge demotion` assertion.
Current code already satisfied every new invariant, so this increment closes
proof gaps rather than fixing a false acceptance. Normal and ASan/UBSan MIR
host CTest pass 5/5, the full compiler mutation campaign has one passing
baseline plus 28/28 killed mutants, and all 136 Python script tests pass.
This remains a test-only increment; regenerate the immutable aggregate ledger
before claiming new overall branch totals.

Wave 52 fixes a real, independently reproduced defect, found by a fresh
post-wave-51 coverage checkpoint collection rather than by any targeted proof
increment. Compiling `tests/mir-clobber/cmpw4.c`'s `CMPW45_EXTRA_HELPER_CALL`
variant (a genuinely empty `static void` helper, MIR shape `insns=1
values=0`) immediately after a normal-sized function fataled under
`DCC_MIR_CACHE_VERIFY=1`: `; MIR CACHE MISMATCH mir_definition
function=cmpw45_extra_helper value=0 cached=1 uncached=-1`. `mir_definition`,
`mir_value_use_count`, and `mir_call_uses_value` bounds-check their cached
arrays against `mir_use_cache_count_capacity` /
`mir_use_cache_arg_head_capacity`, monotonically non-shrinking high-water
marks, but `mir_ensure_use_cache`'s reset loop only zeroed indices up to the
*current* function's own, possibly smaller, `next_value`/`next_call_id`. A
function with fewer values or calls than an earlier one left high indices
holding the earlier function's cached def-index/arg-head answers, readable
as if legitimately defined for the new, smaller function. Reproduced
directly: `DCC_MIR_CACHE_VERIFY=1 ./dcc -DCMPW45_EXTRA_HELPER_CALL
tests/mir-clobber/cmpw15.c -o ...` fataled before the fix and exits 0 after.
This is a real latent miscompile risk even in ordinary, non-cache-verified
builds: any pass consulting these caches for an index a small function never
used could silently receive an unrelated instruction from an earlier
function. The fix clears the full allocated capacity on every cache rebuild
instead of only the current function's smaller count.

A new permanent host control,
`verify_use_cache_capacity_reset_across_functions`, reproduces the exact
shape directly in `tests/host/mir_verify.c`: a normal function defines value
0 at instruction 1, then an immediately following, genuinely empty
zero-value function must not see that stale definition. Reverting the fix
was confirmed to make this control fail (`MIR verifier failures=1`); with
the fix restored, `MIR verifier failures=0`. A full stack/no-stack selector
census against the immediately preceding parent tree shows zero changed
selections, zero changed output, and zero apps requiring runtime validation
across all 3,039 functions, confirming this was a latent, previously
undetected bug rather than a change to any existing production selection or
output. Normal and ASan/UBSan MIR host tests pass, the full compiler
mutation campaign has one passing baseline plus 28/28 killed mutants, both
strict stack/no-stack full+extended release gates pass with zero failures
and zero performance regressions, and all 136 Python script tests pass.

Wave 53 closes the next `mir_match_byte_math_flags` proof gaps without
changing production code. The earlier byte-math coverage already proved the
exact baseline, the broad Wave 19 field-mutation census, and the source-level
mask/compare/complement/add/overflow/logic near matches, but it did not keep
a focused campaign over prototype-bearing helper acceptance, variadic
call-flag instruction-metadata drift, or top-level non-void/VLA shape
rejection, and it left several named fallback reasons unpinned in the
standalone audit. `tests/mir-clobber/bytemath.c` now adds ANSI helper,
compare-variadic, decimal-variadic, non-void-return, and VLA source variants.
A new `byte-math-wave53-audit.py` campaign then runs 24 stack/no-stack and
peep/nopeep runtime controls, retaining exact selection for the baseline and
prototype-bearing helper variants while forcing generic fallback for the four
new near matches, and rejects 7/7 targeted MIR mutations that cover
instruction-metadata type drift, wide-store width, state-pointer typing,
compare/decimal call indirection, and both early return-value paths. Current
code already accepted or rejected every new control as intended, so this
increment closes proof gaps rather than fixing a false acceptance. All 136
Python script tests pass. This remains a test-only increment; regenerate the
immutable aggregate ledger before claiming new overall branch totals.

Wave 60 closes the next `mir_match_catalan_driver_schedule` proof gaps
without changing production code. The earlier Catalan coverage already proved
the exact baseline, the alternate `_pflio` full-I/O exact path, renamed
helpers, unsigned/volatile source near matches, and the broad Wave 23
compile-only field census, but it did not keep a focused runtime-backed
campaign over helper-identity drift across the `zero`/`is_zero`/`add_term`/
`div_small` families, fixed-print and wrapped-`putchar` near matches, or the
remaining metadata, initializer, report, and print-loop legality checks. New
`catalan-wave60` MIR-clobber cases add six source near matches plus 11
runtime-safe selector-mutant cases, and all 72 target configurations pass. A
new `catalan-wave60-audit.py` campaign then runs 28 stack/no-stack and
peep/nopeep runtime controls and rejects 11/11 targeted MIR mutations that
cover helper-identity drift, metadata and array-initializer mutations, both
loop headers and tails, the initial report argument source, and the
outer-print, inner-print, digit, and newline tails. Current code already
rejected every new near match and mutation, so this increment closes proof
gaps rather than fixing a false acceptance. All 136 Python script tests pass.
This remains a test-only increment; regenerate the immutable aggregate ledger
before claiming new overall branch totals.

Wave 62 closes the next `mir_match_symbol_find_schedule` proof gaps without
changing production code. The earlier symbol-find coverage already proved the
exact baseline, the broad Wave 21 field-mutation census, capacity and memory
boundaries, unsigned globals/fields/indexes, volatile table rejection, and
comparison-call global-clobber safety. It did not keep a focused runtime-backed
proof over unsigned return shape, variadic compare and error helpers,
void/variadic copy helpers, count/table address escapes, or several remaining
type, dataflow, helper-identity, and boundary predicates. New
`symbol-find-wave62` MIR-clobber cases add seven source near matches plus ten
runtime-safe selector mutants, and all 72 target configurations pass. A new
`symbol-find-wave62-audit.py` campaign runs 32 stack/no-stack and peep/nopeep
runtime controls and rejects 19/19 targeted MIR mutations covering unsigned
scalar and member-pointer types, PHI/loop/compare/copy/store/return dataflow,
direct helper identity/indirection, and the memory-limit upper boundary.
Current code already rejected every new near match and mutation, so this
increment closes proof gaps rather than fixing a false acceptance. All 136
Python script tests pass. This remains a test-only increment;
regenerate the immutable aggregate ledger before claiming new overall branch
totals.

Wave 63 closes the next `mir_match_ptr_condition_main` proof gaps without
changing its accepted program set or generated schedule. The matcher now runs
its complete semantic signature after the explicit constant, call, ABI,
global, alias, and aggregate-layout checks. This preserves the catch-all proof
while allowing targeted mutations to exercise the specific rejection branches
that the earlier signature placement masked. `tests/tptrcnd.c` adds
runtime-safe alternate init/fail/check/picker helpers, a volatile failure
counter, and renamed-global and loop-picker alias controls. A new
`pointer-condition-wave63-audit.py` campaign runs 52 stack/no-stack and
peep/nopeep controls, retaining exact selection for the baseline,
static-global, and fastcall-picker variants while forcing named spilled
fallback for ten near matches. It rejects 19/19 targeted MIR mutations covering
byte promotion, operations, memory and index layout, constants, call and
argument identities, globals, local/global/function aliasing, initialization,
and return layout, with zero meaningful survivors. No genuine false acceptance
was found: the baseline assembly SHA-256 remains
`8816c4cf4df3c7a7bccb69042a4a814d5c72ea979642204b95fcc23f896ffbfb`
and the selected hash remains `d4d8d799`. The standalone audit, full Python
script suite, and focused strict stack/no-stack `tptrcnd` release gates pass.
Regenerate the immutable aggregate ledger before claiming new overall branch
totals; the broader coverage objective remains incomplete.

Wave 64 closes the next `mir_match_float_tangent_rational` proof gaps without
changing production code. Historical commit `16e9a5f9` introduced the exact
schedule before focused per-matcher audits, and no dedicated tangent fixture
or audit existed. New `tests/mir-clobber/tanrat.c` isolates the
114-instruction schedule and checks zero, signed, quadrant-inverted, and
period-reduced results against seven independently computed mathematical
tangent values. New `float-tangent-wave64-audit.py` runs 12 stack/no-stack
and peep/nopeep runtime controls, retaining exact selection for the baseline
and proving spilled fallback for extra-arithmetic opcode/shape and
variadic-remainder ABI near matches. It rejects 13/13 targeted MIR mutations
covering parameter and call types, local width and identity, call arguments,
repeated constants, negation,
period/quadrant/rational/result dataflow and operators, zero-result flow, and
the final PHI. The clean baseline retains selected hash `948521bf` and
assembly SHA-256
`e0acbc154ecc7efc3491c561749c56a7ea4ece401de8e14b89955a11421db41c`.
Current code already rejected every near match and mutation, so no genuine
false acceptance was found. The standalone audit and all 136 Python script
tests pass. This remains a test-only increment; regenerate the immutable
aggregate ledger before claiming new overall branch totals.

Wave 65 closes the next `mir_match_exec_recursion_schedule` proof gaps without
changing production code. The existing Wave 14 fixture and clobber campaign
already proved every hardcoded binary operator and word-dataflow load through
its 127-mutation audit, plus selected call ABI, memory-width, volatile-result,
and source-expression boundaries. It did not retain a focused runtime-backed
campaign over renamed valid controls, pointer and reporter ABI drift,
return/parameter signedness, recursive-local volatility, extra CFG/VLA shape,
or several remaining entry, vector, call-argument, constant, identity,
side-effect, branch-value, and return predicates. The new
`exec-recursion-wave64` clobber group adds 12 source controls and 35
runtime-safe selector mutations; all 94 target configurations pass. A new
`exec-recursion-wave65-audit.py` campaign runs 48 stack/no-stack and
peep/nopeep runtime controls, retains exact selection for the baseline and
renamed-helper/global variants, and rejects 35/35 targeted MIR mutations with
the expected named reason and `spilled-scalar-cfg` fallback. The baseline
retains selected hash `794b3954` and assembly SHA-256
`28bb381ce9849a6df141705e538c20d53dc48d7588199ec41b0f3fda37a5765a`.
Current code already rejected every new near match and mutation, so no genuine
false acceptance was found. The standalone audit and all 136 Python script
tests pass. This remains a test-only increment; regenerate the immutable
aggregate ledger before claiming new overall branch totals.

Wave 70 closes the historical `mir_match_whitespace_scan_schedule` proof gap
without changing production code. Commit `0401e793` introduced the exact
schedule before focused per-matcher audits, and no dedicated fixture or
campaign remained in the tree. New `tests/mir-clobber/wsscan.c` isolates the
60-instruction, eight-block schedule and validates bounded, empty, multiline,
and helper-mutated state with independent cursor, line, and helper-call
oracles. New `whitespace-scan-wave70-audit.py` runs 16 stack/no-stack and
peep/nopeep runtime controls, retaining exact selection for the baseline and
renamed-helper forms while proving hybrid generic fallback for variadic-helper
and extra-CFG near matches. A forced hybrid candidate cost report confirms the
clean scheduled candidate is the exact incumbent before diagnostic selection.
The audit rejects 25/25 targeted MIR mutations covering bound signedness and
dataflow, source/index/byte access, helper identity and ABI, short-circuit
flow, post-call reloads, newline comparison, both state updates, and member
overlap/range checks. The clean selected hash remains `0259e664` and assembly
SHA-256 is
`31c83b5f4d79640c9908a480717fa7a952afd7d570e3069daebc3860cb92850b`.
Current code already rejected every near match and mutation, so no genuine
false acceptance was found. The standalone audit and all 136 Python script
tests pass. This remains a test-only increment; regenerate the immutable
aggregate ledger before claiming new overall branch totals.

Wave 71 closes the historical `mir_match_random_wide_fill` proof gap and fixes
genuine selector false acceptances. New `tests/mir-clobber/rndwide.c` isolates
the 37-instruction, four-block schedule and checks eight deterministic wide
results against a fixed oracle. New `random-wide-fill-wave71-audit.py` runs 24
stack/no-stack and peep/nopeep runtime controls, retaining exact selection for
baseline and renamed-helper forms while proving spilled generic fallback for
helper-width, count-signedness, volatile-destination, and volatile-temporary
near matches. A forced `spilled-phi-slot` candidate cost report confirms the
clean scheduled candidate is the exact incumbent before diagnostic selection.
The matcher now proves exact scalar types, branch and increment dataflow,
unobservable local-store identity, indirect memory width and volatility, and
the direct helper ABI. The audit rejects 38/38 targeted MIR mutations with
zero meaningful survivors; before the fix, mutations of the branch condition,
increment step, local identities, and multiple type fields retained the
unchanged exact schedule. The clean selected hash remains `2dc38a8d` and
assembly SHA-256 is
`464c9dc3af8d8081d2548e3049bae1cf16623bf6309f31a387c95db0f7662a7c`.
The standalone audit, all Python script tests, and both strict 506-app release
modes pass. Regenerate the immutable aggregate ledger before claiming new
overall branch totals.

Wave 80 closes the historical `mir_match_fixed_embedding_build` proof gap
without changing production code. Commit `9299371d` introduced the exact
schedule before focused per-matcher audits, and no dedicated fixture or
campaign remained in the tree. New `tests/mir-clobber/fxembd.c` isolates the
77-instruction, seven-block schedule and validates all 128 embedding outputs,
including 16 lower and 16 upper saturations, with an independently indexed and
clamped oracle. New `fixed-embedding-wave80-audit.py` runs 12 stack/no-stack
and peep/nopeep runtime controls, retaining exact selection for the baseline
while proving named spilled fallback for volatile-token and variadic-clamp
near matches. Exact and fallback `DCC_MIR_COST_REPORT` assertions plus forced
`DCC_MIR_SELECT_CANDIDATE=spilled-phi-slot` controls verify candidate identity.
The audit rejects 24/24 targeted MIR mutations covering global/local identity,
initialization, PHI and loop bounds, indexing, widths, pointer increments,
signed-wide promotion/addition, call ABI/dataflow, result storage, and loop
updates, with zero meaningful survivors. The clean selected hash remains
`e979e278` and assembly SHA-256 is
`399f0d1374c4e85d8da4744ccaf930010a55a07c7169e4675822bdb36ec7f2e4`.
Current code already rejected every near match and mutation, so no genuine
false acceptance was found. The standalone audit and all 136 Python script
tests pass. This remains a test-only increment; regenerate the immutable
aggregate ledger before claiming new overall branch totals.

Wave 82 closes the historical `mir_match_packed_byte_report_schedule` proof
gap without changing production code. No dedicated fixture or audit previously
covered this exact schedule. New `tests/mir-clobber/pkbrpt.c` isolates its
34-instruction, one-block shape and checks an asymmetric four-byte packing
result against an independently computed runtime oracle. New
`packed-byte-report-wave82-audit.py` runs 20 stack/no-stack and peep/nopeep
runtime controls, retaining exact selection for baseline and renamed-helper
forms while proving spilled generic fallback for volatile, word-width, and VLA
buffer near matches. A forced hybrid candidate cost report confirms the clean
scheduled candidate is the exact incumbent before diagnostic selection. The
audit rejects 26/26 targeted MIR mutations covering buffer type and identity,
lane indices, address dataflow, stride and memory width, byte constants, store
operands, pack-call identity/arguments/directness/result width, print
string/arguments/directness/result width, and the zero return. The clean
selected hash remains `c1803b97` and assembly SHA-256 is
`6c82b5625ae46dd0fef759670274f489aa0656b8b9a0ffc77e17b1f921c1e47b`.
Current code already rejected every near match and mutation, so no genuine
false acceptance was found. The standalone audit and all 136 Python script
tests pass. This remains a test-only increment; regenerate the immutable
aggregate ledger before claiming new overall branch totals.

Wave 90 closes the remaining focused
`mir_match_call_safe_member_sum_schedule` proof gaps without changing
production code. The existing Wave 17 clobber manifest already covered the
exact baseline, local and aliasing callees, volatile and CFG near matches, and
13 broad field mutations. New `call-safe-member-sum-wave90-audit.py` adds 32
stack/no-stack and peep/nopeep runtime controls, retaining exact selection for
baseline, local-callee, and aliasing-callee forms while proving named spilled
fallback for variadic, different-callee, padded-record, and volatile-loop-state
near matches. The audit rejects 47/47 new MIR mutations covering word types
and widths, parameter and local identities, initializers, PHIs, loop condition
dataflow, member layout and loads, direct call structure, every staged call
sum, raw-member accumulation, increment, and final store flow. Forced
`DCC_MIR_SELECT_CANDIDATE=spilled-phi-slot` output independently confirms every
generic fallback, with zero meaningful survivors. The clean selected hash
remains `99a108e6` and assembly SHA-256 is
`865bd210a681c96c6ee1d767a513ab0ca24e169818f8d86e378a3de9c8be0534`.
Current code already rejected every new near match and mutation, so no genuine
false acceptance was found. The standalone audit and all 136 Python script
tests pass. This remains a test-only increment; regenerate the immutable
aggregate ledger before claiming new overall branch totals.

Wave 100 closes the historical `mir_match_gnarly_runner` proof gap and fixes a
genuine exact-schedule false-acceptance class. The existing matcher proves the
564-instruction opcode fingerprint, 22-block CFG, constants, value/PHI
relationships, conversions, string identities, direct, indirect, and variadic
call ABI, array aliases and strides, structure copy, object identities, and
return flow. Fresh diagnostic mutations showed that binary and PHI result
types plus array and structure-member pointer types were not part of that
proof, allowing unsupported type-mutated candidates to retain the hard-coded
schedule. Those type and width invariants are now explicit.

New `tests/mir-clobber/gnarly.c` preserves the exact main-function shape and
adds an independent helper-side runtime oracle for the Duff copy, structure
argument, implicit call, repeated function-pointer call, and old-style sum.
New `gnarly-runner-wave100-audit.py` runs eight stack/no-stack and peep/nopeep
runtime controls, retains exact selection for the baseline, and proves named
spilled fallback for a volatile-count near match. It rejects 22/22 targeted
MIR mutations and independently reproduces every fallback with forced
`DCC_MIR_SELECT_CANDIDATE=spilled-phi-slot` output. The clean selected hash
remains `a855a26c` and assembly SHA-256 is
`dc95b353f36d0e1245a68d34b22f3ed7fafdff6bdba02a991fbd1fecb22cbfbb`.
The standalone audit, all Python script tests, and both strict 506-app release
modes pass. Regenerate the immutable aggregate ledger before claiming new
overall branch totals.

Wave 150 closes the historical `mir_match_float_atan2_schedule` proof gap and
fixes a genuine exact-schedule false-acceptance class. The existing matcher
proved its 66-opcode fingerprint, ten-block count, selected constants, partial
value flow, parameter offsets, and common unary helper identity. It did not
prove the exact CFG label relationships, comparison and float result types,
all branch and return operands, ratio-local identity and width, parameter-load
identity, or direct-call ABI. Twenty-four meaningful diagnostic mutations of
those fields retained the unchanged hard-coded schedule before the fix. Those
structural, type, storage, dataflow, and call invariants are now explicit.

New `tests/mir-clobber/fatan2.c` isolates the schedule and checks nine
independently tabulated results covering zero, both vertical axes, all four
quadrants, and asymmetric coordinates. New
`float-atan2-wave150-audit.py` runs 20 stack/no-stack and peep/nopeep runtime
controls, retains exact selection for the baseline, and proves named spilled
fallback for variadic-helper, volatile-parameter, volatile-ratio, and
different-helper near matches. It rejects 34/34 targeted MIR mutations and
independently reproduces each fallback through
`DCC_MIR_SELECT_CANDIDATE=spilled-phi-slot`, with zero meaningful survivors.
The clean selected hash remains `77d2fef8` and assembly SHA-256 is
`98d3715cb1c1fcca5d9801b075cf97fda73eb079bbb35a0e8bbf37d24ab22213`.
The standalone audit, all Python script tests, and both strict 506-app release
modes pass. Regenerate the immutable aggregate ledger before claiming new
overall branch totals.

Wave 140 closes the historical
`mir_match_matrix_product_schedule_kind` proof gap for both its transposed and
outer variants and fixes a genuine exact-schedule false-acceptance class. The
existing matcher proves the 101- and 90-instruction opcode fingerprints,
seven-block loop structure, parameter layout, pointer and counter dataflow,
direct conversion and clamp calls, and output stores. Fresh diagnostic
mutations showed that named-memory widths, pointer/count/word/long result
types, binary operand types, and indexed/indirect access types were not fully
proved, allowing 73 of 85 representative type and width mutations to retain
the unchanged hard-coded schedule. Those invariants are now explicit for both
kinds, and the transposed clear call also validates its complete call and
argument metadata.

New `tests/mir-clobber/matkind.c` exercises both exact source shapes and checks
their results against independently calculated fixed-point matrix oracles.
New `matrix-product-kind-wave140-audit.py` runs 12 stack/no-stack and
peep/nopeep runtime controls, retains both clean exact schedules, isolates a
source near match for each kind, and rejects 95/95 targeted MIR mutations (49
transposed and 46 outer). Forced
`DCC_MIR_SELECT_CANDIDATE=spilled-store-address` output independently confirms
every generic fallback with zero meaningful survivors. The clean selected
hashes remain `88cdd4b5` and `eca25a44`; combined fixture assembly SHA-256 is
`b3d565637021d445de314147095b9e7aba6fcd7a5de036f89e130d952a263452`.
The standalone audit, full Python script-test suite, and both strict 506-app
release modes pass. Regenerate the immutable aggregate ledger before claiming
new overall branch totals.

Wave 170 closes the historical `mir_match_aggregate_field_sum` proof gap and
fixes genuine exact-schedule false acceptances. The matcher already constrained
the one-block opcode counts, three-leaf addition tree, nonvolatile scalar
loads, common aggregate parameter location, field offsets, widening
conversions, and wide return. Fresh diagnostic mutations showed that aggregate
parameter types, aggregate-address and member-pointer types, member widths,
and load widths were not consistently proved. Those gaps let 24 of 34
representative mutations retain the exact schedule, including cases where the
emitter changed the number of bytes read from a field.

The matcher now validates the aggregate parameter and its three addresses,
requires scalar member-pointer and load types to agree, bounds every field
inside the aggregate, matches member/load widths, and proves the wide addition
result and operand types. New `tests/mir-clobber/aggfsum.c` isolates the
16-instruction schedule with signed-byte, signed-long, and unsigned-word
fields and checks two asymmetric results against fixed independent values.
New `aggregate-field-sum-wave170-audit.py` runs 24 stack/no-stack and
peep/nopeep runtime controls, retains exact selection for baseline, renamed,
and padded-layout forms, and proves generic fallback for volatile, pointer,
and four-field near matches. It rejects 34/34 targeted MIR mutations and
independently reproduces every fallback with forced
`DCC_MIR_SELECT_CANDIDATE=spilled-phi-slot` output. The clean selected hash
remains `bcb40981` and assembly SHA-256 is
`3e00c6e0a6c77e7df08684782db10084f71c228e2b1ec29cff820914bd86d7e3`.
The standalone audit, full Python script-test suite, and both strict 506-app
release modes pass. Regenerate the immutable aggregate ledger before claiming
new overall branch totals.

## Full workload

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
marker, and executes through both the normal generic selection and an explicitly
forced regional candidate in stack/no-stack and peep/nopeep modes. This proves
that the regional emitter falls back from its adjacent-byte micro-optimization
without contaminating or declining the otherwise valid candidate. Host controls
add a successful spilled preflight followed by an oversized-frame rejection and
cover the dense-switch width query. The wide-narrow multiply cache is verified
on both `tlongopt` and the canonical `tm1mu.mulmod` shape.

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
One hundred eighty-six `ln2` field mutations cover every named log-series
rejection group and its retained array/local identity, constant, type, width,
and SSA operand predicates while executing the original program generically;
five source-level reorderings provide independent controls. Both mutator
functions have exact line, branch, and region coverage.

One hundred forty-four byte-math field mutations exercise retained parameter,
mask, comparison, memory, call, decimal, subtract, add, carry, overflow,
logical, negative, zero-flag, and SSA operand checks. The original runtime
program is restored before generic emission, so all 576 target configurations
validate fallback output. The full clobber manifest contains 4,576 target
configurations.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,359 / 4,359 | 100.00% |
| Lines | 176,883 / 189,969 | 93.11% |
| Native branch outcomes | 89,884 / 144,548 | 62.18% |
| Regions | 158,969 / 169,908 | 93.56% |

The mutation framework adds two fully covered functions and reduces missing
lines, branch outcomes, and regions by 37, 26, and 23 respectively. The raw
ledger now contains 54,417 uncovered outcomes.

A second assignment review removed a preempted 2-D address branch, a
pointer-array result path whose plain assignment had already returned, and a
final member fallback already owned by the earlier member-pointer and
member-array cases. Direct controls retain multidimensional long/float
acceptance plus dead-result and nonzero-pointer rejection behavior. Both
stack modes retain byte-identical selectors and selected hashes for all 3,039
functions.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,359 / 4,359 | 100.00% |
| Lines | 176,885 / 189,940 | 93.13% |
| Native branch outcomes | 90,196 / 144,518 | 62.41% |
| Regions | 158,971 / 169,867 | 93.59% |

The second pruning reduces missing lines, branch outcomes, and regions by 27,
33, and 39 respectively, with no target-output or performance change. The raw
ledger now contains 54,384 uncovered outcomes. The expanded byte-math field
matrix and SSA sweep cover another 128 branch outcomes, and the per-condition
log matrices reduce the remaining ledger by another 181. The raw ledger is now
54,075.

The multidimensional-array schedule now has a named accepted control plus 93
restored-MIR mutations spanning roots, layout, members, strides, check calls,
byte/word loads, loops, initializers, aliasing, returns, and summary strings.
All 376 target configurations execute the original `t2darr` program through
the expected exact or generic selector.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,359 / 4,359 | 100.00% |
| Lines | 176,916 / 189,941 | 93.14% |
| Native branch outcomes | 90,301 / 144,518 | 62.48% |
| Regions | 158,987 / 169,867 | 93.59% |

This adds 105 covered branch outcomes, 31 lines, and 16 regions while adding
only one source line for acceptance diagnostics. The raw ledger now contains
53,970 uncovered outcomes.

The narrowed div/mod schedule now has a named accepted `tdmfuse` control plus
125 restored-MIR field and SSA mutations. All 504 target configurations either
select the exact schedule or require the spilled generic selector and preserve
the 66-check runtime result.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,359 / 4,359 | 100.00% |
| Lines | 176,927 / 189,942 | 93.15% |
| Native branch outcomes | 90,428 / 144,518 | 62.57% |
| Regions | 158,996 / 169,867 | 93.60% |

This adds 127 covered branch outcomes, 11 lines, and nine regions while adding
only one acceptance-diagnostic line. The raw ledger now contains 53,843
uncovered outcomes.

The recursive byte MinMax schedule now has a named accepted control plus 79
restored-MIR mutations spanning move accounting, constants, winner dispatch,
locals, loop state, board mutation, recursive calls, and maximizing/minimizing
paths. All 320 target configurations preserve the one-iteration target oracle.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,359 / 4,359 | 100.00% |
| Lines | 176,960 / 189,943 | 93.16% |
| Native branch outcomes | 90,511 / 144,518 | 62.63% |
| Regions | 159,010 / 169,867 | 93.61% |

This adds 83 covered branch outcomes, 33 lines, and 14 regions while adding
only one acceptance-diagnostic line. The raw ledger now contains 53,760
uncovered outcomes.

The Catalan driver now has a named accepted control plus 110 restored-MIR
field and SSA mutations. All 444 target configurations preserve the canonical
100-digit output, using the documented 768-byte stack requirement.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,359 / 4,359 | 100.00% |
| Lines | 176,985 / 189,944 | 93.18% |
| Native branch outcomes | 90,622 / 144,518 | 62.71% |
| Regions | 159,022 / 169,867 | 93.62% |

This adds 111 covered branch outcomes, 25 lines, and 12 regions while adding
only one acceptance-diagnostic line. The raw ledger now contains 53,649
uncovered outcomes.

The ctype/realloc schedule now has a named accepted control plus 32
restored-MIR mutations spanning allocation, failure, copy, grow, preserve,
byte-store/check, shrink, free, and final-result proofs. All 132 target
configurations preserve the canonical `ctype/realloc ok` result.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,359 / 4,359 | 100.00% |
| Lines | 177,002 / 189,945 | 93.19% |
| Native branch outcomes | 90,654 / 144,518 | 62.73% |
| Regions | 159,030 / 169,867 | 93.62% |

This adds 32 covered branch outcomes, 17 lines, and eight regions while adding
only one acceptance-diagnostic line. The raw ledger now contains 53,617
uncovered outcomes.

The prime-search schedule now has a named accepted control plus 71 restored-MIR
mutations spanning parameter ABI, local layout, initialization, argument
conversion, odd normalization, loop divisibility, and final reporting. All 288
target configurations preserve the ten-prime output.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,359 / 4,359 | 100.00% |
| Lines | 177,017 / 189,946 | 93.19% |
| Native branch outcomes | 90,730 / 144,518 | 62.78% |
| Regions | 159,039 / 169,867 | 93.63% |

This adds 76 covered branch outcomes, 15 lines, and nine regions while adding
only one acceptance-diagnostic line. The raw ledger now contains 53,541
uncovered outcomes.

One hundred thirty-two fixture-backed historical attention mutations now
exercise the matcher's explicit index, width, type, constant, and SSA checks
while requiring the spilled generic fallback and preserving all 14 accuracy
checks in both stack modes.

| Metric | Covered / total | Percent |
| --- | --- | ---: |
| Functions | 4,359 / 4,359 | 100.00% |
| Lines | 177,027 / 189,946 | 93.20% |
| Native branch outcomes | 90,863 / 144,518 | 62.87% |
| Regions | 159,049 / 169,867 | 93.63% |

The complete test-only attention sweep adds 133 branch outcomes and ten
lines/regions. The raw ledger now contains 53,408 uncovered outcomes.

- Review the remaining 53,408 unreviewed raw uncovered outcomes rather than
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
