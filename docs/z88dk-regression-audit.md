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

The initial pass was compile-oriented.  A subsequent adapter pass built all
898 GCC torture execute tests as CP/M programs and ran successful builds under
ntvcm.  Before runtime fixes, 679 passed, 198 failed to build, and 21 built but
failed at runtime.  This is a compatibility census rather than a claim that
every failure is a dcc bug: sources can depend on unsupported features, a
32-bit `int`, implementation-defined behavior, or SDCC harness assumptions.

Of the 194 GCC-import compile failures, 127 are immediately accounted for by
dcc's explicit diagnostics for unsupported `double`, `long double`, or `long
long`.  Other non-actionable groups include z88dk/SDCC ABI attributes, far and
named address spaces, inline-assembly syntax, flexible array members, and
public-symbol collisions caused by M80's six-significant-character linker
limit.  The remaining failures need semantic triage and minimization.

## Priority order

### P0: internal compiler failures or silent wrong-code risks

- **Fixed: comparison signedness lost after same-width casts and the usual
  arithmetic conversions.**  MIR records the converted operand type, but
  spilled, homed, and fused comparison emitters re-inferred signedness from
  the values' original definitions.  This miscompiled explicit
  signed/unsigned casts and mixed `int`/`unsigned int` comparisons.  The fix
  raises GCC torture runtime passes from 679 to 683 and removes failures in
  `20080506-1`, `930916-1`, `compare-2`, and `pr28651`.  Covered by
  `tests/tuaccmp.c`.

- **Fixed: aggregate ABI and assignment metadata gaps.**  Old-style struct
  arguments were passed as addresses instead of copied values; global struct
  return destinations were mistaken for IX-relative locals; and stale AST
  symbol slots could turn `*int **` assignment into an aggregate `LDIR`.
  These fixes clear `931005-1` and `lto-tbaa-1`, with coverage in
  `tests/tstrtri.c`.

- **Fixed: pointer-to-array typedef parameters.**  `typedef T A[N]; A *p`
  lost its row dimension and acquired an extra pointer level, so `(*p)[i]`
  interpreted object bytes as an address.  This clears `20080519-1` and is
  covered by `tests/ttdarrp.c`.

- **Fixed: narrow unsigned bit-field promotion.**  Unsigned bit-fields whose
  range fits in 16-bit signed `int` now promote to `int`, while an explicit
  cast to `unsigned int` remains unsigned.  This clears `bf-sign-2` and
  `bitfld-1`, covered by `tests/tbfprom.c`.

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

The runtime-failure queue is now empty.  Four final cases were resolved as
three compiler defects and one exclusion: `20000227-1`, `pr37924`, and
`pr81913` now pass, while `pr86714` explicitly tests implementation quality
after an excess array initializer, whose behavior is undefined by C89.

`920730-1` is now fixed: the target `<limits.h>` gives `USHRT_MAX` and
`UINT_MAX` unsigned suffixes and gives the long limits their required long
suffixes, so the usual arithmetic conversions no longer treat decimal 65535
as a signed `long` in comparisons against `UINT_MAX`.

`pr78675` is now fixed as well.  The spilled backend's linear slot intervals
did not cover a value defined inside a loop but live across its backedge and
used after exit, allowing loop-header temporaries to overwrite it.  Backedge
intervals now use CFG liveness, with focused coverage in `tests/tloopcar.c`.

`arith-rand` is now fixed.  Nested sibling blocks receive stable internal
declaration names, deferred aliases cover the full lexical block rather than
ending at its first conditional branch, and resolved operand types propagate
through unary/conditional MIR.  Equal-width signedness conversions feeding a
store are retained so object promotion cannot replace a signed local with its
unsigned initializer value.  The adapted upstream test passes all 394 runtime
assertions, with focused coverage in `tests/tsibdecl.c`.

`pr37924` is fixed.  An equal-width cast from signed to unsigned was treated
as a representation-only conversion even when it selected the semantics of a
right shift, causing an arithmetic shift where C requires a logical shift.
Such conversions are now retained for the left operand of `>>`, with focused
coverage in `tests/tprewrap.c`.

`pr81913` is fixed.  Prefix decrement of an unsigned byte wrapped its stored
low byte correctly, but a later integer promotion reused the unnormalized
16-bit intermediate (`0xffff` instead of 255).  Byte-to-word casts now always
perform the required sign or zero extension; `tests/tprewrap.c` covers the
original loop and constant values.

`20000227-1` is fixed.  Global character-array initialization walked decoded
string data as a NUL-terminated host string, so an embedded `\0` truncated
the initializer and inferred array bound.  It now uses the lexer's recorded
byte length.  `tests/tnulstr.c` covers the escaped form, and direct compilation
of the original ISO-8859 source confirms that both `\377` and a literal
0xff byte emit `{ 0, 255, 0 }`.

`cmpsf-1` is fixed.  Its comparison helpers had correct float-typed MIR, but
the lazy-parameter allocator admitted 4-byte parameters even though that path
only reliably materialized 1/2-byte values.  A comparison could push the first
float and then treat the second as already resident, effectively comparing
against stale registers.  Wide parameters now use the established direct-home
path, with focused two-float-parameter coverage in `tests/tfpcmp2.c`; the full
upstream 8-by-8 table passes all 384 assertions.

`doloop-1` and `doloop-2` now pass unchanged after the unsigned limit and
promotion corrections above.  `pr86231` exposed a separate conditional bug:
MIR tested only the base-type bits and mistook a `void *` result for a void
expression, discarding the selected pointer before assignment.  Covered by
`tests/tptrcond.c`.  `scope-1` is also fixed: a block-scope `extern` now creates
a lexical alias to external storage rather than allocating an uninitialized
automatic object.  Covered by `tests/tscpext.c`.

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

The focused regressions pass under ntvcm in both optimized and unoptimized
builds.  The complete dcc correctness suite after the fixes has 433 runnable
applications and 12 intentionally skipped; the extended, diagnostics, and
dccpeep-fixture checks pass.  Performance baselines are refreshed when a
corrected ABI or promotion path legitimately changes generated code.
