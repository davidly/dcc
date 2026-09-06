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
```

Both fixtures run in release, full debug, and line-debug modes, with and
without peephole optimization and stack checks.