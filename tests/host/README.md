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

The tests cover operand and object bounds, dimensions, opcodes, branch labels,
definition uniqueness, PHI references, call identities, argument positions,
and known direct/indirect-call ABI types. They also reject non-dominating
ordinary values, PHI-edge operands, and call arguments, while accepting valid
backedges, unreachable predecessor paths, irreducible CFGs, and definitions
that dominate their uses despite appearing later in the instruction array.
The harness includes the driver under a
different entry-point name so it links the real compiler state and verifier.

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

## Generated Differential Matrix

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