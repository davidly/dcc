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
and known direct/indirect-call ABI types. Valid diamond and loop-backedge PHIs ensure
forward references remain supported. The harness includes the driver under a
different entry-point name so it links the real compiler state and verifier.

This is not a full SSA dominance proof. An experimental definite-definition
analysis exposed loop promotion's use of UNREACHED at function entry.
Changing entry objects to UNDEFINED passed output tests but caused 22 checked
cycle/size regressions across eight applications, including substantial losses
in trwold. That experiment was removed; promotion and exact schedules must be
addressed together before enabling full dominance enforcement.

Target execution and volatile access-count/flag assertions are covered by:

```sh
pwsh ./scripts/run-mir-clobber-tests.ps1 -Cases semantics
```