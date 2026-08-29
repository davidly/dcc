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

- **Closed: no remaining genuine fatal after harness normalization.**  The
  initial GCC pass reported `MIR emission is required` for `pr51581-1` and
  `pr51581-2`, but those translation units had already entered an SDCC-only
  inline-assembly block.  With `PORT_HOST` defined, both original files
  compile cleanly: their ordinary signed/unsigned division and remainder code
  is valid on dcc's 16-bit target, while the strength-reduction functions are
  correctly excluded by their 32-bit-int/64-bit-long-long target guard.  This
  was a harness false positive rather than an independent dcc defect.

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

`Issue_1167_choosing_which_function.c` is fixed.  Function designators now
decay to function pointers in dcc's pointer-expression analysis, allowing the
result of a conditional expression such as `(pick ? first : second)(arg)` to
flow through the established indirect-call lowering path.  Covered by
`tests/tfncond.c`.

`Issue_2478_dropped_type_arith.c` is fixed.  Integer-to-pointer casts now
accept long integer operands, which is required for decimal address constants
above 32767 on dcc's 16-bit-int target.  Pointer arithmetic and dereference in
local initializers, including commuted `integer + pointer`, then use the
existing pointer-expression lowering path.  The same correction fixes
`02_addr_ptr.c` and `const_cast_to_pointer.c`, including constant and variable
indexing of byte and word pointers at absolute addresses.  Covered by
`tests/tptarith.c`.

`Issue_2523_global_init.c` is fixed.  The global-initializer parser now
recognizes relocatable `symbol + offset` address constants behind optional
pointer casts and parentheses instead of sending every leading-parenthesis
form to the numeric constant folder.  Numeric grouping remains on its
existing path.  Covered by `tests/tginaddr.c`.

`Issue_1409_offset_pointer_initialisation.c` is fixed.  Relocatable
`symbol +/- offset` initializers now consume a complete typed integer constant
expression instead of one numeric token, and scale the result by the symbol's
element size.  This covers pointer tables initialized with products and
parenthesized arithmetic; the focused test uses compatible pointer base types
rather than the upstream test's `uint16_t *`/`uint8_t[]` mismatch.  Covered by
`tests/tginoffs.c`.

`Issue_1054_initialisation.c` is fixed.  Global scalar initializer paths now
unwrap optional brace levels recursively around array scalar elements,
including string literals used for pointer elements.  Multidimensional character-array rows
initialized by strings are copied inline with their terminating NUL and row
padding instead of being recorded as string-address relocations.  Covered by
`tests/tinitbr.c`.

`Issue_493__func__.c` is fixed as a selected C99 addition.  Within a function
body, the predefined `__func__` identifier now yields the function's complete
source-level name as a character string, independently of M80 assembly-name
shortening.  Covered by `tests/tfuncid.c`.

GCC torture execute `20020503-1` is fixed.  Assignment through a dereference
lvalue now accepts a long-valued right operand when the pointed-to object is a
byte or word scalar.  The existing generic store already performs C's required
low-byte/low-word narrowing; only the AST support gate had rejected the form.
This restores the test's `*--p = '0' + value % 10` integer-formatting loop,
covered by `tests/tldref.c`.

GCC torture execute `20030128-1` is fixed.  Dead-result arithmetic compound
assignment to a global byte now uses the same direct-symbol load/combine/store
path that already handled global-byte bitwise compound assignment.  Operands
undergo integer promotion and the result narrows back to the byte, including
the upstream `unsigned char /= volatile short` case.  Covered by
`tests/tgbcmp.c`.

The applicable core of GCC torture execute `20030714-1` is fixed as a selected
C99 addition.  Dcc now permits `_Bool` bit-fields with C99's required maximum
width of one bit and preserves their boolean type for assignment
normalization.  The original translation unit still needs its two colliding
`Render...setStyle` public names shortened differently for M80, so focused
mixed-field coverage is provided by `tests/tboolbf.c`.

GCC torture execute `20060929-1` is fixed.  Pointer-valued postfix updates on
dereference lvalues now pass the AST support gate at any pointer depth, and the
shared address-based postfix emitter scales the stored pointer by its element
size instead of always adding or subtracting one byte.  This covers the
upstream `*(*p++)++ = *q++`, discarded-read, and discarded-update forms, with
focused coverage in `tests/tnestpi.c`.

GCC torture execute `20080424-1` is fixed.  A row selected from a
multidimensional array now decays in pointer comparison operands for arrays of
any rank, rather than only two-dimensional arrays.  Dynamic and postfix row
indexes have focused coverage in `tests/trowcmp.c`.  Broader first-level
partial-decay coverage across global and local 3-D arrays, byte/word/long
elements, function parameters, `sizeof`, address equivalence, outer pointer
arithmetic, and side-effecting indexes is provided by `tests/tnddecay.c`.

GCC torture execute `921019-1` is fixed.  File-scope pointer initializers may
now take the address of a constant-index element of a string literal, through
optional grouping and a pointer cast.  The initializer records the literal's
relocation plus the element offset; zero and nonzero offsets have focused
coverage in `tests/tstraddr.c`.

GCC torture execute `930526-1` is fixed.  Local array direct-declarators may
now carry redundant grouping parentheses, including the upstream
`int *(p[3])` array-of-pointers spelling.  Grouped scalar arrays and one- and
two-dimensional byte, word, and long pointer arrays have focused coverage in
`tests/tparrgrp.c`.

GCC torture execute `950809-1` is fixed.  Pointer modifiers in a struct member
declarator list now apply to each declarator independently, so a declaration
such as `int *sp, scalar, *sc, values[2]` no longer makes every later member a
pointer.  Mixed byte, word, long, pointer, scalar, and array fields have
focused coverage in `tests/tflddecl.c`.

GCC torture execute `960326-1` is fixed.  Aggregate type specifiers no longer
prematurely end declaration-specifier parsing, so storage classes and
qualifiers may follow tagged or anonymous struct/union specifiers, including
the upstream `struct s static s1` ordering.  Focused initializer and zero-fill
coverage is provided by `tests/taggordr.c`.

GCC torture execute `const-addr-expr-1` is fixed.  File-scope address constants
may now combine a grouped array-to-pointer base, scaled constant pointer
arithmetic, and a following struct member suffix, such as
`(int *)&((items + 1)->value)`.  Zero and nonzero element offsets and multiple
member types have focused coverage in `tests/tmemaddr.c`.

GCC torture execute `pr39240` is fixed.  A function returning `signed char` or
`unsigned char` now accepts supported 16- and 32-bit integer expressions and
applies the return type's narrowing conversion, rather than requiring an
identifier to already have byte type or a literal to be nonnegative and below
256.  Focused coverage is provided by `tests/tretnar.c`.

GCC torture execute `pr44468` is fixed.  `sizeof(type)` nodes now participate
in the shared scalar and strict constant folders, matching their existing
classification as integer constant expressions.  This allows folded `&&` and
`||` guards combining `sizeof` and `offsetof`; focused coverage is provided by
`tests/tszlogic.c`.

GCC torture execute `pr60017` is fixed.  File-scope struct initializers now
preserve every dimension of array fields and compute the correct row stride,
including multidimensional character arrays and arrays whose elements are
structs.  Focused nested-brace and value coverage is provided by
`tests/tnestini.c`.

The portable compile gap exposed by GCC torture execute `20050826-2` is fixed.
Pointer arithmetic rooted at a pointer declared inside a nested block now
retains its not-yet-scoped pointer possibility during early AST validation,
so an assignment such as `table[index] = item + 1` through a pointer-to-pointer
parameter is not misdiagnosed as integer-to-pointer conversion.  Focused
runtime coverage is provided by `tests/tppasgn.c`.

GCC torture execute `pr66233` is fixed.  The generic indexed-lvalue assignment
path now accepts float-valued right operands for byte and word integer elements
and performs the required float-to-destination-integer conversion before the
store.  Focused global-array loop coverage is provided by `tests/tfixarr.c`.

GCC torture execute `pr67929_1` is fixed.  Integer-returning functions now
admit supported float expressions and let MIR apply the function return type's
float-to-integer conversion, including the truncation toward zero required by
C.  Focused positive and negative conversion coverage is provided by
`tests/tfret.c`.

A false fallthrough diagnostic exposed while triaging GCC torture execute
`20010409-1` is fixed.  Direct calls to the C library's non-returning
`longjmp`, `exit`, and `abort` functions now terminate the enclosing statement
path for function-end analysis, so a non-void wrapper ending in one does not
warn that control reaches its closing brace.  Focused `setjmp`/`longjmp`
runtime coverage is provided by `tests/tnoretrn.c`.

The remaining GCC torture execute `20010409-1` expressions are fixed as well.
A one-dimensional array of structs now undergoes array-to-pointer decay when
used as the identifier base of `->`, rather than being rejected or loaded as
though its first object bytes held a pointer.  This enables chained reads,
assignments, calls, and postfix updates such as
`holder->items[holder->index]->field++`.  The original module compiles
unchanged, with focused runtime coverage in `tests/tchptr.c`.

GCC torture execute `pr70127` is fixed.  Structure assignment chains are now
lowered recursively instead of supporting only two destinations, preserving
the assigned object value through expressions such as
`dest = local = array[0] = source`.  The original module compiles unchanged,
with focused loop, array-lvalue, local, and bit-field coverage in
`tests/tstchain.c`.

The portable declaration gap in GCC torture execute `930513-1` is fixed.
Old-style (K&R) function definitions now accept function-pointer parameter
declarations and retain their prototypes for indirect-call checking and
lowering.  The original module compiles unchanged, with focused runtime
coverage in `tests/tkrfnptr.c`.

The multidimensional character-array initializer gap exposed by GCC torture
execute `strlen-2` and `strlen-3` is fixed.  String literals now initialize
complete innermost rows under standard aggregate brace elision, including both
`{"1", "12"}` and nested three-dimensional forms.  Both upstream files now
parse past their declarations; focused byte and zero-fill coverage is provided
by `tests/tmdstr.c`.

The nested-array-typedef portion of GCC torture execute `strlen-4` is fixed.
Array typedefs now retain their complete dimension vector when composed through
further typedefs, rather than collapsing each alias to one flat length.  This
gives global initialization, indexing, row strides, and `sizeof` the original
array shape.  The upstream initializer now parses successfully; focused
runtime coverage is provided by `tests/ttdstr.c`.

The following pointer-to-array-typedef declarations in `strlen-4` are fixed as
well.  Adding a pointer declarator to an array typedef now produces a pointer
object with the typedef's row-stride metadata, rather than misclassifying the
object itself as another array.  Constant addresses such as `&values[1]` also
scale their first subscript by the full multidimensional row size.  The
upstream pointer initializers now compile, with address coverage added to
`tests/ttdstr.c`.

Explicit dereference-and-subscript expressions on those pointers are fixed:
`(*p)[i][j]` is normalized to its C-equivalent `p[0][i][j]` in the AST, so it
uses the established multidimensional pointer indexing path and preserves each
typedef-defined stride.  Focused optimized and unoptimized value checks are
included in `tests/ttdstr.c`.

Pointer arithmetic on a dereferenced pointer-to-array is fixed as well.
Row-decay metadata now survives binary `+`/`-`, so expressions such as
`*((*p) + i)` scale by the typedef-defined row width and the selected row then
decays correctly as a call argument.  The corresponding first group in
`strlen-4` now compiles, with runtime call-argument coverage in
`tests/ttdstr.c`.

The equivalent subscript spelling in `strlen-4` is fixed too.  A first
subscript of a pointer to a multidimensional array typedef, as in `p[0]`, now
retains its array-row decay and stride rather than loading the row's first byte
as a scalar.  Consequently `*(p[0] + i)` selects and passes the intended inner
row.  Focused optimized and unoptimized coverage is included in
`tests/ttdstr.c`.

Partial multidimensional subscript chains through those pointers are fixed as
well.  A form such as `(*p)[row]` (normalized internally to `p[0][row]`) now
remains an array row and decays to `char *`, allowing the following byte offset
in `(*p)[row] + offset`.  This clears the next `strlen-4` group, with focused
zero and nonzero row/offset coverage in `tests/ttdstr.c`.

The pointer-table group in `strlen-4` is fixed next.  For an object such as
`Pair *table[2]`, dcc now records the table's own bound separately from the
multidimensional array typedef targeted by each pointer.  The table therefore
allocates and initializes two pointer elements, while expressions such as
`*(*(table[index]) + row)` still retain the pointed-to row stride.  Focused
forward, zero, and backward row selection, including a following byte offset,
is covered by `tests/ttdstr.c`.  This completes the distinct standard-C
expression groups in `strlen-4`; `strlen-5` through `strlen-7` intentionally
exercise out-of-bounds string traversal and are excluded as undefined.

GCC torture execute `20001011-1` is fixed as a selected dialect addition.
The GNU `__FUNCTION__` predefined identifier now shares dcc's existing
source-function-name implementation for standard `__func__`, including its
array behavior under `sizeof` and independence from M80 name shortening.  The
original module compiles unchanged, with focused coverage in `tests/tfuncid.c`.

GCC torture execute `20021118-1` exposed missing C brace elision for an array
member of a struct.  Both automatic and static initializer parsers consumed
the comma following the array's last element as though it were still inside
the array, instead of leaving it for the enclosing struct.  They now stop at
the completed unbraced subobject; focused storage-duration and following-field
coverage, including flat initialization of outer arrays of these structs, is
provided by `tests/tbrelide.c`.  This also clears extended c-testsuite case
`00205` in both optimized and unoptimized modes.

GCC torture execute `20060102-1` now compiles unchanged.  dcc predefines the
widely used implementation macro `__CHAR_BIT__` as 8, matching the target's
fixed byte width and `limits.h`'s `CHAR_BIT`.  Preprocessor and signed-shift
expression coverage is provided by `tests/tcharbit.c`.

dcc now also predefines `__SIZE_TYPE__` as `unsigned int`, the underlying type
of its 16-bit target `size_t`.  This clears the otherwise-supported GCC torture
execute modules `20031012-1`, `20041112-1`, `20050215-1`, `20050218-1`,
`20050502-2`, `20050826-1`, and `20071219-1` unchanged.  Focused type width,
unsigned conversion, `sizeof`, and prototype coverage is in `tests/tsizetyp.c`.

GCC torture execute `930930-1` now parses fully after defining the matching
signed 16-bit `__PTRDIFF_TYPE__` as `int` and accepting GCC's diagnostic-only
`__extension__` marker as an empty predefined macro.  Focused positive and
negative pointer subtraction and type-width coverage is in `tests/tptrmac.c`.

GCC torture execute `970214-2` exposed token-insensitive function-like macro
substitution.  A parameter named `L` was replaced inside the single wide
character token `L'1'`; ordinary quoted replacement-list text could likewise
be scanned for parameter names.  Macro expansion now copies narrow and wide
quoted tokens atomically.  Focused coverage is in `tests/tmacwide.c`.

GCC torture execute `pr30778` now compiles unchanged after mapping GCC/SDCC's
`__builtin_offsetof` spelling to dcc's existing `__offsetof` constant-expression
implementation.  `tests/tbuiltof.c` verifies the upstream use case by clearing
only the struct prefix before a nonzero sentinel field.

A wrong-code bug found while minimizing the SDCC follow-up cases is fixed in
spilled-CFG phi copies.  Slot allocation can give a parameter and a phi result
the same backend slot number, but parameter loads still read the parameter's
named IX-relative home; treating that numerical match as an already-completed
identity copy left the phi slot uninitialized.  Ternaries such as
`condition ? parameter : fallback` could consequently return stale data.
`tests/tphiarg.c` covers both CFG edges with parameter-valued arms.

GCC torture execute `970217-1` exposed accepted-code wrong behavior for C99
variable-length array parameters.  Array adjustment correctly changes
`T parameter[bound]` to `T *parameter`, but dcc also discarded a runtime bound
expression in a function definition, omitting required side effects such as
`count++`.  Runtime parameter bounds are now evaluated on function entry;
focused coverage is in `tests/tvpapar.c`.

`doloop-1` and `doloop-2` now pass unchanged after the unsigned limit and
promotion corrections above.  `pr86231` exposed a separate conditional bug:
MIR tested only the base-type bits and mistook a `void *` result for a void
expression, discarding the selected pointer before assignment.  Covered by
`tests/tptrcond.c`.  `scope-1` is also fixed: a block-scope `extern` now creates
a lexical alias to external storage rather than allocating an uninitialized
automatic object.  Covered by `tests/tscpext.c`.

### P2: diagnostics and selected dialect additions

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
builds.  The complete dcc correctness suite after the fixes has 465 runnable
applications and 12 intentionally skipped; the extended, diagnostics, and
dccpeep-fixture checks pass.  Performance baselines are refreshed when a
corrected ABI or promotion path legitimately changes generated code.
