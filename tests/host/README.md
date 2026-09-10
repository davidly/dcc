# Host MIR Verifier Tests

Build and run the verifier's malformed-input tests from the repository root:

```sh
cmake -S src/dcc -B build/mir-tests -DDCC_BUILD_MIR_TESTS=ON
cmake --build build/mir-tests --target mir-verify-test --parallel
ctest --test-dir build/mir-tests --output-on-failure
```

For Clang/GCC sanitizer coverage, configure with
`-DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` and run
CTest with `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`.
An additional build with `-DCMAKE_C_FLAGS=-fwrapv` exercises offset rejection
without allowing the host compiler to assume signed overflow is impossible.

The tests cover operand and object bounds, dimensions, opcodes, branch labels,
definition uniqueness, PHI references, call identities, argument positions,
and known direct/indirect-call ABI types. They also reject non-dominating
ordinary values, PHI-edge operands, and call arguments, while accepting valid
backedges, unreachable predecessor paths, irreducible CFGs, and definitions
that dominate their uses despite appearing later in the instruction array.
The host fixture directly asserts that PHI operands are live only on their
own incoming edges and that an argument stays live into, but not after, its
matching call. Scalar and aggregate indirect calls must carry a callee value.
An unprototyped local callback is authoritative even when a differently
prototyped global has the same spelling; verification must not import the
hidden global's ABI or arity.
The harness includes the driver under a
different entry-point name so it links the real compiler state and verifier.
It also directly exercises MIR stream block I/O, cursor-relative/end seeks,
short reads, copying, file transfer, hashing, zero-sized operations, invalid
seeks, and null close behavior. These are candidate-isolation primitives, so
their tests assert exact bytes and cursor state rather than merely executing
the helpers.

Small verified MIRs also assert cross-module query defaults and reversible
state scopes: affine constants, PHI/candidate counters, CSE/value-numbering
no-ops, lazy/rematerialized allocation state, strict PHI fallthrough, and
address-rematerialization probes. Separate fixtures verify exact parameter
load text (HL, DE, wide, and IY prologue), named-member type/layout resolution,
isolated static-global field addresses, and five-argument call recovery.
Candidate-state assertions cover every public homed/spilled feature flag at a
clean function boundary. Positive transform controls forward a PHI-return join
onto its two predecessor exits and eliminate duplicate address expressions in
both block and region passes; each test asserts rewritten value identities and
reruns MIR verification.
Additional controls cover PHI-return forwarding through a scalar consumer,
isolated static-global field value numbering, block CSE of field loads, and
the scalar-DAG output contract. The consumer case verifies that both inserted
consumers use their predecessor's value and that each inserted return uses the
new consumer result after instruction storage grows.
The AST support matrix also checks malformed assignment rejection plus
identifier, long, float, pointer, array, and every compound-assignment operator
without relying on parser filtering. Pointer-index controls include
pointer-array elements, dereferenced pointer-to-array rows, computed pointer
expressions, multidimensional pointer elements, and pointer-valued members,
with nonzero-pointer and compound-assignment rejection cases. Direct
multidimensional long/float controls preserve the common lvalue-type gate that
preempts narrower shape-specific fallbacks.
The spilled-emitter preflight matrix independently rejects oversized frames,
invalid aggregate return/value widths, unsupported opcodes, unresolved memory,
invalid indirect widths, malformed direct/indirect calls, and invalid
`va_arg` offsets before code emission. Successful and rejecting controls also
exercise exact-shape, selector, and backend-slot diagnostic reporting.
The two-byte `va_list` pointer must fit entirely within IX displacements
[-128, 127], so `MIR_VA_ARG` accepts starting offsets -128 through 126.
Out-of-range offsets, including `LONG_MIN` and `LONG_MAX`, must reject without
writing text or consuming labels. Repairing only the offset and retrying in
the same stream, without resetting function or analysis state, must produce
the same bytes as a clean valid candidate at either supported boundary.

`dcc_mir_verify.c` constructs an independent CFG and immediate-dominator tree
using reverse postorder. Its storage is linear in the MIR size. Verification
runs after object promotion and semantic transformations, before allocation
and candidate emission. PHIs define values at their logical block entry;
their operands must dominate the corresponding incoming predecessor edges.
Unreachable edges impose no dominance requirement, but structural checks
still apply to their IDs and references. There is no environment switch that
disables the dominance check.

Object promotion distinguishes an undefined function-entry object from an
unreached dataflow state. A value available only from a loop backedge cannot
supply the entry path. Such values remain memory accesses unless promotion
can establish a valid merge. Tests cover both initialized and undefined
entry objects. Reading an uninitialized C local is not made defined by this
change; it is simply no longer represented as a non-dominating SSA value.

The earlier entry-state experiment lost exact schedules because their proofs
expected dead PHIs. Those opcode/relationship tables now describe the corrected
NOP positions, with the substantive operation, ABI, and CFG checks retained.
Performance baselines must remain unchanged.

Target loop execution and volatile access-count/flag assertions are covered by:

```sh
pwsh ./scripts/run-mir-clobber-tests.ps1 -Cases semantics
pwsh ./scripts/run-mir-clobber-tests.ps1 -Cases domloop
pwsh ./scripts/run-mir-clobber-tests.ps1 -Cases aliasmem
pwsh ./scripts/run-mir-clobber-tests.ps1 -Cases qualexpr
pwsh ./scripts/run-mir-clobber-tests.ps1 -Cases qualgen
```

These fixtures run in release, full debug, and line-debug modes, with and
without peephole optimization and stack checks.

Exact-selector near matches additionally require three pieces of evidence for
the intended function: a named template rejection, absence of an exact
scheduled-machine selection, and a selected homed/hybrid/regional/spilled
generic emitter. Harness controls reject unrelated-function or exact-selection
markers. `iyexact` is the positive word-table schedule control; `iynear`
changes only the loop's initial table index, must reject that schedule, and
executes through generic generated code in all stack and peephole variants.

`aliasmem` checks writes through identical and distinct pointers, conditional
alias writes, mutating calls, and `memcpy`. Its MIR assertions require volatile
array-member and nested-member accesses to retain their count, byte width,
and volatile flags, including stores. Nonvolatile controls must still reuse
repeated loads and combine adjacent little-endian bytes. Runtime output alone
cannot detect a removed volatile read when the backing memory stays unchanged.

Pointer qualifier regressions cover direct and typedef-based parameters,
old-style parameters, globals, block locals, static locals, and pointer fields.
They distinguish volatile byte reads from volatile intermediate pointer reads,
including a pointer to a volatile pointer to volatile bytes. Nonvolatile
controls detect qualifier leakage. A block-local double-pointer case also
checks deferred pointer-word type repair and byte-index scaling.

Declaration, symbol, typedef, and field metadata retain a
`pointee_volatile_mask`: bit zero describes the immediate pointee, bit one the
next pointee, and so on. Adding a pointer shifts existing levels and records
the previous object's qualifier. MIR loads shift the address mask back one
level; member addresses combine the field's own qualifier with its pointee
mask. This preserves the distinction between a volatile pointer and volatile
data without replacing the existing type encoding or debug metadata format.

## Generic MinMax Regression

The clobber runner's `minimax` case forces baseline, all-optimization, and
address-rematerializing spilled candidates for `tests/ttt.c:MinMax`. It asserts
spilled selection and 6,493 moves for the standalone default single iteration,
in both stack configurations, both peephole modes, and release/full-debug/line-debug
builds. Explicit `DCC_MIR_SELECT_CANDIDATE` diagnostics can override an exact
incumbent; ordinary production selection is unchanged.

This exposed a peephole interaction hidden by the exact schedule: byte zero-test
rewrites discarded a live zero/sign-extended `HL` result after a subsequent
`ld h,0` had been removed as redundant. The rewrite now requires `HL` dead;
the signed form also requires `A` and parity dead. Small assembly fixtures cover
live-result preservation and the still-valid unsigned dead-result rewrite.

The signed rule tracks the differing `A`/parity values with a 512-visit CFG
budget. Z/NZ branches observe equivalent flags; `or a` preserves the differing
byte until it is overwritten. Pair increment/decrement and 16-bit addition
do not kill parity. Unresolved branches, user assembly, and unknown instructions
or calls reject the proof. Local callees must overwrite the incoming values
before using them. Reviewed DCCRTL entries (`__fpc`, printf variants, `__ssf`,
`__scat`, `_atoi`) discard those incoming values; `__stchk` only kills the flag
difference and preserves `A`. At a generated C return, `A` and parity are not
result channels. These refinements retain the existing performance baseline
without weakening the required `HL` deadness proof.

## Qualifier Expression Matrix

`qualexpr.c` checks explicit and typedef casts, adding/removing/restoring
volatile qualifiers, void-pointer casts, deep pointer casts, conditional
qualifier merging, direct and inline pointer returns, deep pointer returns,
and nonvolatile prototype-return controls. MIR assertions check access counts,
widths, and which pointer level is volatile separately from runtime output.
Qualifier-removal tests use objects originally declared nonvolatile; they do
not read a volatile-defined object through an unqualified lvalue.

Pointer casts carry their target qualifier mask in the AST and MIR. An
explicit zero mask overrides the source qualifiers. Identity conversion
elimination must preserve this distinction. Function symbols retain return
qualifiers, and inline expansion preserves both cast metadata and the declared
pointer-result contract.

Indirect-return cases cover parameter and deferred block-local function
pointers, a global function-pointer typedef, explicit `(*getter)()` calls,
and byte/word access widths. A volatile function-pointer object returning
ordinary bytes, with a volatile-pointer parameter in its prototype, checks
that neither qualifier leaks into the returned data. Prototype parsing must
preserve the enclosing declarator's qualifiers. The function-pointer level
shifts the return mask left; MIR indirect calls recover it by shifting the
callee value's mask right. Deferred call type repair also updates subscript
element types and widths before indirect memory operations are finalized.

The abstract/deep matrix additionally covers:

- Explicit and typedef abstract casts, including functions returning `T **`.
- Function-pointer typedef aliases, arrays, struct fields, and deferred locals.
- Factories returning function pointers, through named, raw, and abstract forms.
- Distinct factory/returned-function prototypes (`int` versus `long` arguments).
- Nested callback parameters in abstract prototypes.
- Full-width indirect `long` results, high-word preservation across another call,
  and defined unsigned carries at 15-, 16-, 31-, and 32-bit boundaries.

Named and abstract function pointers share declarator parsing. Prototype suffix
parsing preserves the enclosing declaration state. `funcptr_return_type` retains
the actual return type separately from the saturated two-level pointer encoding;
`funcptr_result_prototype` retains the signature when that result is callable.
Anonymous prototype records live in a translation-unit arena, not a temporary
statement arena, so field metadata and retained inline ASTs cannot outlive them.
The active AST classifier and MIR lowering share `ast_call_result_type()`.

The additional metadata does not expand the target's ordinary data-pointer
encoding or claim exhaustive C declarator coverage. It preserves both supported
data-pointer levels when a function-pointer declarator would otherwise consume
one, and keeps callable layers and their argument ABIs distinct.

Preserving scalar return types also exposed a previously truncated vtable
`long` result. The full-width path is retained. Materialized frame-based long
addition now uses a byte carry chain into `DE:HL`, then the existing result
forwarding machinery, avoiding stack shuffles. The byte minimax exact schedule
checks the explicit byte return after identity-conversion elimination. Both
stack configurations and peephole modes remain within checked baselines.

## Generated Differential Matrix

The seeded `fuzz` case complements the fixed `qualgen` matrix:

```sh
pwsh scripts/run-mir-clobber-tests.ps1 -Cases fuzz
pwsh scripts/run-mir-clobber-tests.ps1 -Cases fuzz -FuzzSeeds 23117
pwsh scripts/new-mir-fuzz-source.ps1 -OutputPath build/replay.c -Seed 23117 -Programs 1
pwsh scripts/test-mir-fuzz-source.ps1
pwsh scripts/run-mir-compiler-mutations.ps1
pwsh scripts/run-mir-compiler-mutations.ps1 -Jobs 2 -BuildJobs 2 -OutputDirectory build/mir-mutations-parallel
```

Three default seeds generate 12 functions each, mixing six arithmetic steps,
8/16-bit memory, aliased and non-aliased indirect writes/calls, live values across
calls, zero-to-three-iteration loops, and conditional joins. Eight boundary
inputs per function produce 96 assertions per seed. Host arithmetic explicitly
masks target 16-bit results; divisors are nonzero, right shifts are 0..7, indices
stay inside four elements, and all wrapping arithmetic is unsigned. This is a
bounded grammar-based fuzzer, not unrestricted C generation or an exhaustive
type/control-flow matrix.

The ordinary twelve build configurations yield 3,456 reference comparisons.
Two forced generic candidates for `fuzz0`, in both stack and peephole modes,
add 2,304 checks of the full programs (only `fuzz0` is forced). Result-corruption
controls must fail all 96 assertions and return failure in each of four release
configurations per seed; those controls test oracle sensitivity, not compiler
mutation coverage. Source and build artifacts are retained under
`build/mir-clobber-failure-*` on failure. Case selection accepts comma-separated
names and rejects unknown names rather than silently running no tests.

`DCC_MIR_CACHE_VERIFY=1` is enabled for the seeded suite. Seed 23117 exposed
promotion clearing definition IDs while subsequent promotion queries used a
stale definition cache. Promotion now invalidates that cache after definition
removal and after alias rewrites. The generator's first program is a replayable
regression; source-debugging was unavailable on the development macOS host, so
a temporary native stack probe identified the owning pass and was removed.

The host verifier additionally mutates all 16 populated definition/operand/label
fields of a valid diamond, requires rejection, restores each field, and requires
acceptance. Five call/argument identity mutations have positive controls.
Empty, negative, and oversized dominance graph contracts are tested directly.
The compiler-mutation runner builds isolated copies with dominance, argument
ABI, call-arity, indirect-callee, callback-identity, PHI-edge-liveness,
call-argument-liveness, or PHI-consumer-value checks disabled, or promotion
cache invalidations removed. It first requires unmutated host tests and a one-
function seed-23117 compilation to pass. Verifier mutants must produce explicit
host-test assertion failures; the cache mutant must produce the specific
`mir_definition` cache mismatch and fatal diagnostic. Both kinds of kill require
exit code 1; verifier kills require the exact mutation-specific assertion line
and a nonzero failure summary. The runner invokes the same host executable as
CTest directly, preserving its exit code rather than treating CTest's aggregate
failure code as proof of a kill. Build errors, crashes, timeouts, unrelated
diagnostics, and survivors are not counted as kills.

`-Jobs` defaults to 1 and bounds independent mutant worker processes; the
unmutated host and compile controls must both pass before any mutant starts.
`-BuildJobs` defaults to 2 and bounds each worker's CMake build separately.
Every worker copies its own source, headers, host fixture, build/cache, binary,
and output trees. Each initial build is clean without reusing baseline objects
or compilers. Workers clear inherited `DCC_*` controls and isolate
`LLVM_PROFILE_FILE`; mutant profiles never enter normal compiler coverage.
Both host tests and compile probes explicitly enable `DCC_MIR_CACHE_VERIFY=1`.
The parent process environment is unchanged.

Logs, per-worker artifacts, and inventory-ordered `results.json` are retained
under `build/mir-compiler-mutations` (or `-OutputDirectory`, relative to the
repository root or absolute). Source/build workspaces are removed on completion.
Every inventory entry is recorded even on failure; a failed baseline leaves all
nine mutants invalid/not-run. Worker failures do not cancel other scheduled
mutants, and any invalid result or survivor fails the command. Concurrent runs
must use distinct output directories. These nine controls do not establish a
general compiler mutation score.

Focused runner tests use tiny synthetic compiler fixtures rather than rebuilding
the production compiler:

```sh
python3 -m unittest discover -s scripts/tests -p 'test_mir_compiler_mutations.py'
```

`run-mir-clobber-tests.ps1` records each successful target configuration in an
optional JSON execution manifest and verifies the exact expected count for
full and focused runs. Candidate-forcing cases require both the selector and
the `DCC_MIR_COST_REPORT` candidate identity. Debug MinMax configurations are
kept as generic debug controls because debug emission intentionally uses the
incumbent rather than honoring a forced cost-policy candidate.

### Call Arity Invariants

Each call's argument positions must form a contiguous zero-based set; textual
order may differ. Known nonvariadic prototypes require exactly their declared
parameter count. Variadic prototypes require the fixed prefix and permit extras.
Unprototyped calls have no parameter-count restriction but still require valid
argument positions. Existing duplicate/late-argument and dominance checks remain
independent and required.

Arity lookup covers named global functions and indirect calls whose callee is a
named `MIR_LOAD` or `MIR_PARAM`, matching the existing argument-ABI lookup. The
declared-symbol table retains the variadic flag as well as the parameter count
and types. Unresolved callee values are not assigned a guessed prototype; calls
through casts, fields, PHIs, or returned function pointers need explicit MIR
signature transport before the verifier can enforce their full prototype arity.

Host tests cover missing/excess arguments, holes, extreme argument positions,
zero-argument prototypes, unprototyped calls, reversed argument-record order,
and fixed/variadic local and global callbacks. A compiler mutant disabling
prototype-count enforcement must fail the missing-argument test specifically.

The `structv`, `stringv`, `floatv`, `bitfield`, and `callid` near-match cases now
require explicit rejection of their named schedule and generic selection for
the named function. An unrelated exact schedule elsewhere in the program can
no longer satisfy the assertion. Their expected output includes deliberate
source-level failures where appropriate; this verifies that selection preserves
the changed semantics instead of replaying a recognized successful result.

`qualgen` generates a deterministic C program in the runner's temporary build
directory and compares target results with a host-computed reference table.
It covers 576 combinations: two element widths (8/16 bits), six expression
forms (plain, explicit cast, typedef cast, direct return, conditional, and
qualifier round trip), eight seeds (0, 1, 127, 255, 256, 32767, 32768, 65535),
three indices, and both conditional outcomes.

The reference calculation uses host integers and explicitly masks element
values to 8 or 16 bits and arithmetic results to 16 bits. It does not assume
that the host C compiler has dcc's integer widths. All pointer indexing stays
within four-element arrays, with no numeric-address comparison or dependence
on host pointer size. The unsigned arithmetic is defined, including wrapping;
there is no signed overflow or unsequenced mutation. Each generated check has
a stable index reported on failure. The full clobber CI gate runs both matrices
in all twelve release/debug configurations, for 6,912 generated target checks.

This is a bounded differential matrix, not exhaustive C testing or randomized
fuzzing. In particular, it does not establish complete return-qualifier
transport for arbitrary indirect function calls or every abstract declarator.
