# z88dk and GCC regression audit

This is the working backlog for mining tests from z88dk and from the SDCC
regression corpus used by z88dk's pinned zsdcc build.  It records candidates,
not a promise that every upstream test is applicable to dcc: dcc deliberately
targets C89 plus selected later features on a 16-bit CP/M/Z80 model and does
not implement `double`, `long long`, far-address spaces, or z88dk/SDCC calling
convention attributes.

## Source snapshot and first census

- z88dk: `z88dk/z88dk` master at `c0ca46856636174575b3ec5e804c4530a5f99fff`.
- zsdcc/SDCC: z88dk's pinned `r16639` source archive.
- z88dk `testsuite/*.c`: 122 files; 71 compile unchanged as dcc modules and
  51 stop during compilation.
- SDCC `gcc-torture-execute-*.c`: 898 files; 704 compile as dcc modules and
  194 stop during compilation in the initial compatibility pass.

The first pass is intentionally compile-oriented.  Passing compilation does
not imply that the test's runtime assertions have been imported and executed
under ntvcm yet.  Conversely, an initial compile failure is not automatically
a dcc bug: the source may depend on an excluded feature or on the SDCC test
harness's target-specific macros.

Of the 194 GCC-import compile failures, 127 are immediately accounted for by
dcc's explicit diagnostics for unsupported `double`, `long double`, or `long
long`.  Other non-actionable groups include z88dk/SDCC ABI attributes, far and
named address spaces, inline-assembly syntax, flexible array members, and
public-symbol collisions caused by M80's six-significant-character linker
limit.  The remaining failures need semantic triage and minimization.

## Priority order

### P0: internal compiler failures or silent wrong-code risks

- **Fixed: extern function-pointer array loses its array shape.** z88dk
  `testsuite/Issue_497_astroforce_compile.c` made dcc abort with `MIR emission
  is required`.  An `extern` object declaration did not copy `is_array`,
  dimensions, or element size onto its symbol.  Indexing a table declared via
  a function-pointer typedef consequently used the pointed-to function's
  return size (zero for `void`) instead of the two-byte function-pointer size.
  Covered by `tests/tfpaext.c`.

- **Fixed: loop-backedge value promoted before its definition dominates.**
  SDCC's GCC import `gcc-torture-execute-961004-1.c` exposed an invalid MIR
  use and an internal verification fatal.  Object promotion collapsed an
  entry/backedge merge to the backedge's conditionally-created value, then
  substituted that later definition into an earlier loop load.  Promotion now
  rejects a reaching value defined at or after its use.  Covered by
  `tests/tloopdef.c`.

- **Next: audit remaining genuine fatals after harness exclusions.** The
  initial GCC pass also reported `MIR emission is required` for the pr51581
  cases, but those translation units had already hit unsupported SDCC inline
  assembly in the selected configuration; they are not yet evidence of an
  independent dcc defect.  Re-run all candidates with per-test target guards
  normalized before promoting any of them to bugs.

### P1: standard-C compile gaps with useful Z80 coverage

These z88dk cases appear applicable after removing irrelevant harness details
and should be minimized, host-validated, then turned into runnable dcc tests:

- `02_addr_ptr.c` and `const_cast_to_pointer.c`: dereference/index an absolute
  integer-to-pointer cast.
- `Issue_1167_choosing_which_function.c`: call the result of a conditional
  expression selecting between two function designators.
- `Issue_2478_dropped_type_arith.c`: pointer arithmetic and dereference inside
  a local initializer, including commuted `integer + pointer`.
- `Issue_2523_global_init.c`: parenthesized/cast address constant expressions
  such as `(char *)(array + 1)` in file-scope initializers.
- `Issue_1409_offset_pointer_initialisation.c`: pointer arithmetic in
  file-scope aggregate initializers (with the upstream incompatible base type
  corrected in the minimized test).
- `Issue_1054_initialisation.c`: extra brace levels around scalar/string
  subobjects in aggregate initialization.

### P2: diagnostics and selected dialect additions

- `Issue_493__func__.c`: `__func__` is C99 rather than C89; consider alongside
  dcc's other selected C99 features, not as a base-language bug.
- Classify the remaining GCC-import DCC-E1002/DCC-E1003 failures.  Prefer
  small integer, pointer, control-flow, aggregate, and function-pointer cases;
  reject tests whose essential premise is a deliberately unsupported type or
  target ABI.
- Convert applicable compile-only upstream cases into self-checking programs
  with deterministic output, and run each in both peephole and no-peephole
  modes before adding its baseline.

## Explicit exclusions

Do not spend compiler-fix effort on a test whose essential behavior requires:

- `double`, `long double`, `long long`, `_Float16`, or 64-bit arithmetic;
- z88dk/SDCC fastcall, callee, shortcall, interrupt, naked, banked, or
  register-preservation attributes;
- far pointers, named address spaces, absolute-placement extensions, SFRs, or
  target-specific inline assembly;
- a flexible array member or another language feature dcc intentionally does
  not claim;
- public external names that cannot coexist under M80's six-character symbol
  significance, unless the test can be renamed without changing its premise.

## Validation for completed items

The two minimized regressions pass under ntvcm in both optimized and
unoptimized builds.  The complete dcc suite after the fixes reports 425
passed, 0 failed, 12 skipped; the extended, diagnostics, dccpeep-fixture, and
performance checks all pass.
