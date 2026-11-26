# Host MIR Verifier Tests

Build and run the verifier's malformed-input tests from the repository root:

```sh
cmake -S src/dcc -B build/mir-tests -DDCC_BUILD_MIR_TESTS=ON
cmake --build build/mir-tests --target mir-verify-test --parallel
ctest --test-dir build/mir-tests --output-on-failure
```

## Host regression coverage

Exact numeric-kernel and flagged-record fingerprints canonicalize aggregate
identities in first-use order rather than hashing parser-assigned struct IDs.
Unrelated declarations must not disable optimized schedules. Distinct aggregate
identities, pointer flags, signedness, memory layouts, and CFG payloads remain
part of the existing semantic proofs. Run the declaration-order, layout, and
type-identity controls after building the compiler:

```sh
python3 -m unittest scripts.tests.test_mir_type_fingerprints
```

Directory-enumeration fingerprints also normalize aggregate IDs. The unrelated
`meminfo_t` declaration in `stdlib.h` must not disable the directory schedule.
Controls cover both directory fixtures in whole-program and module builds,
with and without stack checks, and preserve layout and distinct-type rejection.
The Fortran fatal-error schedule has the same declaration-order controls for
both fatal fixtures, including line-debug builds, while retaining its layout
and type-identity rejection checks.

Late integer normalization has target-width constant-tree and demanded-bit
shift controls, including shared/volatile values, bounded proofs, constant
address chains, and release-identical line-debug output. Its fixed-value CP/M
oracles run in peep/nopeep and release/full-debug/line-debug modes:

```sh
python3 -m unittest scripts.tests.test_mir_integer_optimization
```

The same suite covers little-endian word-store combination, exact assembled
writer size limits (25 bytes raw, 20 with peephole), alias-sensitive reloads,
signed/unsigned values, masks, volatile destinations, and both debug modes.
The host verifier additionally checks 33 store-proof cases covering shared
trees, intervening observers and all memory/CFG barriers, including idempotence
and preservation of rejected instructions.

Bit-window controls exhaust all 65,536 word inputs for signed and unsigned shifts
4--7 and enforce assembled size limits in both pipelines. Signed-byte parameter
oracles cover all 256 values with byte-only, word, and shared uses.
Copy-end tests cover zero-length and
boundary copies, observable returns, scaled pointers, and aliased/volatile
counts. Decimal and signed quotient/remainder oracles check negative and
boundary inputs against long arithmetic. The host verifier covers 23 additional
private-word equivalence and early-store variants, including escapes, mutations,
observers, control/stack boundaries, unchanged SSA operands, and idempotence.

Frontend host assertions use the real lexer for nested conditional active-state
transitions and the real type-name parser for nested suffix/token advancement
and bounded EOF behavior. Global-text ownership tests exercise same/different
function writes, absent/multiple writes, and live AST classifier/value-numbering
acceptance or rejection; shared state is restored afterward.

Character assertions exercise the preprocessing evaluator and shared literal
decoder against 22 fixed byte-value oracles, including simple, octal and
hexadecimal escapes and false equality controls.

Target/scheduler host fixtures verify straight-line, diamond/PHI, loop/PHI, and
narrow/wide call-pressure graphs, asserting constraints, CFG/liveness summaries,
legal colors/rematerialization/spills, call splits, and movements. Snapshots
ensure this diagnostic analysis does not alter production state.

The verifier's `--shadow-schedule-require-invalid` subprocess mode builds the
unsupported fixture named `coverage_unsupported` and invokes the schedule
report entry point. REPORT-only diagnoses `valid=0` without terminating;
REQUIRE terminates through the existing fatal path, isolated from normal host
tests. Ordinary driver `#asm` compilation is not an unsupported-MIR oracle.

The five host objects remain identical across JSON, native, and HTML reporting.
`main` and the unexecuted `dcc_driver_main` alias share a source function; do not
call the alias just to raise instantiation coverage. Normal reporting gates
exact nonempty whole-compiler **source-function** totals, not instantiations or
rounded percentages. Existing all-metric selected AST/MIR completeness controls
retain their meaning. See [compiler coverage](../../docs/compiler-coverage.md)
for artifact names, configuration-aware inventory checks, and immutable
checkpoint/report-only provenance.

## Bitset liveness and cache controls

The existing verifier's `--bitset-proof` mode enables
`DCC_MIR_LIVENESS_VERIFY` and checks eight independently constructed live-word
oracles at 1, 2, 63, 64, 65, 127, 128, and 129 values. It compares raw words,
not the compiler's indexing macros, including zero padding and definition/return
boundaries. Each named case and the final zero-failure summary are mandatory.
CTest runs this mode as `mir-bitset-layout`; there are still only five
compiler-linked host binaries.

The liveness diagnostic compares the optimized matrices with the independent
byte fixed point through the existing packed-reference helper. Complete-word
comparison also checks unused tail bits; it does not bypass verifier preflights
or change default allocation/emission policy. Shadow-schedule snapshots use the
actual word-matrix allocation size, not the obsolete instruction-by-value byte
size. The latter caused both incomplete snapshots and an ASan-detected overread
after the upstream storage change.

Compiler entrypoint probes enable each cache/liveness verification control
individually and together, require clean success, and compare assembly/debug
metadata with the controls disabled in all six debug/stack modes. Defined
volatile unsigned increments also provide a small valid control and exact
ordinary/strict oversized-function rejection diagnostics, without a partial
function body or successful assembly footer.

The retained `phi-edge-liveness` and `call-argument-liveness` mutants target the
shipping optimized transfer function. Mutating only the now-diagnostic byte
reference would not challenge ordinary production liveness. Their original
intended assertions and clean-failure classification are unchanged.

## Generated Differential Matrix

The MIR fuzzer runs generated programs against a 16-bit reference model.

```sh
python scripts/run-mir-clobber-tests.py --jobs 8
python scripts/run-mir-clobber-tests.py --cases fuzz --fuzz-seeds 0 1
python scripts/new-mir-fuzz-source.py --output-path build/replay.c --seed 23117 --programs 1
python scripts/test-mir-fuzz-source.py
```

The three default seeds each generate 12 functions and run 96 checks per
configuration. The programs exercise arithmetic, 8-bit and 16-bit memory,
aliasing, live values across calls, bounded loops, and conditional joins.
The reference masks arithmetic to the target widths and generates valid
divisors, shifts, and indices.

Each seed runs in twelve normal configurations, eight configurations that
force a spilled candidate, and four configurations that deliberately corrupt
the result. The corruption controls must fail all 96 checks and return a
failure status. Failures retain the generated source and build logs under
`build/mir-clobber-tests-*`.

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
and fixed/variadic local and global callbacks.
