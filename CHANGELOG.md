# Changelog

## Constant division and predicate/byte lowering (2026-11-26)

Unsigned word division and remainder by powers of two are normalized to
shifts and masks before allocation and helper-clobber planning. This avoids
the stale-quotient interaction that previously prevented division-to-shift
lowering. Promoted unsigned-byte right shifts by 8..15 fold to zero while
retaining argument evaluation and observable memory reads.

Bounded pure byte-return leaves support signed returns, up to three scalar
parameters, constant shifts, and equality expressions. Exact argument types,
side-effect checks, and a bounded lexical preflight keep substitution from
changing conversions, evaluation counts, callbacks, or frame metadata.
Byte-valued word folds use a canonical form; matching captured values and
private snapshots can eliminate redundant byte checks.

Scored MIR alternatives forward fixed global byte stores only across proven
alias-free, dominated regions. Predicate facts retain memory snapshots and
calls; equality guards also support XOR and word affine identities. Pure
unsigned predicate-mask leaves keep their accumulator in L and their input
in DE. Their exact, disjoint bit summaries can prove later scalar checks in
callers redundant. Definitions must precede callers for summary reuse;
overlapping masks, overwrites, aliases and side effects are excluded.

After homed selection, comparison-only unsigned parameters can use a byte
partition when all their predicates are constant on 255..65535. Private
argument copies are saturated once; written, escaped, volatile, forwarded,
wide-return and debugging cases keep their original representation. Byte
range proofs preserve established constant/narrowed-counter paths.

Single-use fixed byte updates use A directly, including expanded assignments.
Unused allocations in strictly stack-independent leaves can then disappear,
allowing the existing IX-frame cleanup to remove the empty frame. Calls,
stack accesses, live allocation addresses and unknown effects block removal.

Unchanged survey sources, no stack checks, peephole optimization, Z80 cycles:

| App | Previous DCC | Current DCC | zsdcc |
| --- | ---: | ---: | ---: |
| tautobyt | 129,770,539 | 13,640,742 | 14,708,228 |
| tbytecmp | 322,299,799 | 163,079,319 | 165,115,271 |
| tbytegen | 346,954,975 | 73,407,867 | 83,520,025 |

Windows/MSVC and Linux/GCC agree on the final counts. A same-source comparison
with the preceding toolchain improves 53 existing apps with peep and 37 without
it. Three peep workloads are slightly slower: pihex +137 cycles (under
0.00003%), tpostfld +14 (0.12%), and tszswap +136,985 (0.15%). Without peep only
pihex is slower, by 142 cycles. The pihex and tpostfld ceilings record these
small tradeoffs; tszswap remains within its existing ceiling.

Added tdivmask, tpredmsk, tguard8 and dead-leaf allocation controls. Full suites
with and without stack checks, the extended suite, 29 Windows regression
apps, 60 focused host tests and 200 differential fuzz programs pass.

## Byte-data loops and invariant parameter arithmetic (2026-11-26)

Small call-free kernels hoist pure unsigned long arithmetic on an unwritten,
unaliased parameter and a literal out of a loop with one entry. Unsigned-byte
do-while comparisons expose their scalar PHIs, and redundant integer
promotions of two unsigned bytes no longer require separate word homes.

Separately initialized bounded loops can share a byte counter when its
original type is unsigned and it explicitly supplies byte data. The proof
rejects intervening counter uses, writes, aliases, jumps, nested declarations,
and bounds that could exceed a byte. Discarded updates of a named byte
counter use its memory home directly. Single-use byte-load equality checks
combine constant addition, subtraction, or bitwise arithmetic without
computing unused high bytes or spilling the comparison operands.

With no stack checks and peephole optimization, unchanged `tprewrap` takes
48,356 Z80 cycles (previously 88,629; zsdcc 52,372), and `trealloc` takes
40,776 (previously 55,113; zsdcc 47,427). Both beat the measured zsdcc builds.
The complete 673-app comparison improves 15 apps in both optimization modes,
with 658 unchanged and none slower. Windows/MSVC and Linux/GCC agree on
target cycle counts. Added `tbytkern` and `tforseq` regression controls for
wraparound, ordering, alternate loop entries, parameter writes and aliases,
volatile reads, shared expressions, counter escapes, and wide bounds.

## Boolean comparisons and bounded byte loops (2026-11-26)

Tiny zero-argument integer comparison leaves can be substituted in MIR while
preserving their global binding and volatile reads. Whole-function declaration
tracking declines substitution across local, enum, or typedef shadowing;
callable bodies remain available for function pointers. A bounded lexical
preflight avoids changing frame metadata while inspecting unrelated bodies.

After candidate selection, homed immediate comparisons use A/flags without
spilling live register pairs, and carry results can become 0/1 directly in
the assigned home. Existing compact zero/sign and nonzero-equality paths, and
wide-return candidates, retain their established lowering. Integer comparison
negation uses the opposite predicate instead of materializing and retesting a
boolean.

Call-free do-while byte updates receive explicit backedge labels for scalar
PHIs. Byte unit updates then stay in their register homes, with redundant high
byte clearing removed when the entry and backedge prove it stays zero.
Constant byte updates used by tests fold with the required byte wraparound.
A synchronized zero-based word counter cannot exceed 256 before a natural
unsigned-byte cycle terminates; matched PHI/backedge proofs remove impossible
early-return guards. Nonzero initial values, different steps, nested cycles,
resets, calls, and volatile counters retain their guards.

Long induction-variable decrements use borrow chains in their PHI slots.
Proven 0/1 additions to long accumulators use conditional carry chains;
ordinary +1 updates retain the existing direct path. Constant-left long
comparisons exposed by type repair are canonicalized again before emission.

At 4 MHz without stack checks, peep timings improve as follows:

| Test | Previous DCC seconds | DCC seconds | zsdcc seconds |
|---|---:|---:|---:|
| tbytepre | 0.03808650 | 0.01930550 | 0.02290050 |
| tcmpbyte | 31.23386950 | 20.69122550 | 21.51568675 |
| tforlong | 0.01023075 | 0.00686825 | 0.00743725 |

The MSVC/Windows and GCC/Linux builds produce identical peep cycle counts and
output for all three. `timhome` exhaustively checks immediate comparison
boundaries; `tboolin` covers lexical bindings, volatility, callbacks, and side
effects; `tlongphi` covers carry/borrow boundaries, live old values, and signed
controls; `tbytrng` covers synchronized-loop bounds and required exclusions.
Performance ceilings are updated from measured results without loosening
unrelated baselines. The host logical-widening test now accepts either valid
zero extension or generic signed extension for a proven 0/1 signed-byte value.

## Byte results, long updates, and rotates (2026-11-26)

The MIR backend emits only the demanded byte of pure parameter/constant
bitwise return expressions, including signed-byte return extension. Wide
integer counters can update globals directly through carry/borrow chains in
loops; aliases, volatile accesses, live update results, and intervening calls
or stores retain their ordinary paths. Complementary logical shifts of the
same unsigned long become constant rotates, using byte permutations and short
bit rotations. Forwarded long addition/subtraction chains read stable frame
operands directly instead of repeatedly pushing the accumulator.

At 4 MHz without stack checks, peep timings improve from 62.6493 to 26.8391
seconds for `tternarg`, 0.07071 to 0.02986 for `tloopcar`, 0.11346 to 0.07617
for `twalkend`, 0.05746 to 0.03881 for `twalksc`, and 0.09067 to 0.08739
for `tlongexp`. Compiler and runtime interfaces are unchanged.

Tiny pure static byte helpers can also be substituted automatically in MIR
when argument types match exactly and their evaluation can be reused safely.
Word-comparison and byte-return caller contexts retain compact call paths.
Embedded low/high-byte XORs then collapse to a byte fold. Long branch-only
loads are tested directly, and loop comparisons forward operands across
non-emitting constants. Proven 0/1 PHI values widen with a zero extension.
`tloopcar` now uses 119,447 cycles versus zsdcc's 119,947; `tternarg` uses
107,356,234 versus zsdcc's 108,754,398.
`tautobyt` adds exhaustive inline-byte checks and argument-conversion,
side-effect, callback, volatile and snapshot controls. `tlongtr` covers direct
long truth tests and zero/one widening, with signed controls.

Unsigned comparisons against multiples of 256 test the high byte for
register/computed operands. An unsigned byte load compared with an embedded
byte fold branches directly without widening and spilling both operands.
Nonescaping local pointers initialized once in the entry prefix can
rematerialize fixed global addresses in loops; reassignment, volatility,
address escape, conditional initialization and shadowing retain ordinary
paths. Profitability guards preserve existing compact named-load and
non-loop candidates. These changes reduce `tternarg` from 41.5345 to 26.8391
seconds. `tbytecmp` exhaustively checks comparison boundaries and byte folds;
`tfixptr` checks pointer rematerialization and its safety exclusions.

`tbytegen` checks all 65,536 word inputs, signed extension, nested operands,
and observable effects. `twidegen` checks every constant rotate count against
literal oracles, carry/borrow boundaries, live update results, alias/call
barriers, and volatile controls. Both have output and performance baselines.
The typed-expression fuzz runner now pairs each worker outcome with its seed
before reporting it, fixing a reporting exception after successful workers.

## Full-range square root (2026-11-16)

`sqrtf` now reduces positive finite arguments to `[1, 4)` before Newton
iteration and restores the scale afterward. This fixes both large arguments
(`sqrtf(1e4f) = 100`, `sqrtf(1e8f) = 10000`) and tiny arguments, including
subnormals, and restores `asinf`/`acosf` accuracy near ±1. Raw-bit
classification and a byte iteration counter offset the range-reduction cost.
The unchanged `tsqrtsp` test uses 44% fewer cycles with the same COM size.
`tsqrtrg` adds 1,323 checks across every normal exponent, subnormal powers
and significands, special values, and inverse-trig endpoints, with a new
performance baseline. All existing performance baselines remain unchanged.
The full and extended suites, diagnostics, and peephole fixtures passed in
both optimization modes with and without stack checking; the standard run
reported zero cycle-count or COM-size regressions.

## 16-bit div/mod fusion across switch cases (2026-11-16)

Div/mod fusion now requires both operations to be in the same basic block.
Previously, 16-bit operations with shared operands could fuse across exclusive
switch cases, leaving a case to read a temporary initialized only by another
case. For example, `100 % 7` could return a stale value instead of `2`.
Valid fusion within one block is preserved. `tdmcfg` covers signed and unsigned
switch cases in both operator orders, changing operands, conditionals, and
early returns; code-generation tests check that same-block pairs still fuse.
Validation: all 40 integer optimization tests passed. Strict full and extended
suites passed with and without stack checking (588 apps passed, 24 skipped
per run), along with diagnostics and peephole fixtures. Checked performance
baselines passed, and `tdmcfg` has a measured baseline. Apple Clang 21 matched
its expected output at `-O0`, `-O2`, and with UBSan.

## Heap allocation performance (2026-11-16)

`malloc` now starts at a conservative first-free hint, skipping the known
allocated prefix while retaining first-fit order. Free, coalescing,
heap trimming, and in-place realloc keep the hint valid. Programs without heap
allocation pay no startup cost. An isolated 1,000 × `malloc(16)` benchmark
dropped from 86,856,931 to 908,931 Z80 cycles (96× faster); 500 allocations
take 475,931 cycles. A follow-up removed eager hint maintenance on allocation
and heap-top growth, uses zero to represent a heap with no holes, and tightens
the fragmented-heap traversal. All workloads whose cycle counts regressed in
the initial hint commit now beat the allocator before that commit in both
optimization modes with the current compiler. Some linked programs still use
an extra 128-byte COM record for the hint code. Fragmented heaps still require
a first-fit search.
`tmallseq` covers sequential allocation, early-hole reuse, retained small
holes, coalescing, realloc absorption, shrink tails, and heap-top growth.

## Clang-oracle differential campaign (2026-11-13 → 2026-11-16)

A differential fuzzing campaign against Apple clang, started after the
miscompiles fixed by #219 and #220 showed that the unit-test corpus was not
catching serious wrong-code bugs. Every reproducer is defined C whose host
output does not depend on `int` width, proven with the local clang and clean
under ASan/UBSan; every fix was gated on the strict full+extended regression
suites with no performance-baseline regressions.

**Totals:** 69 bugs logged (BUG-001 … BUG-069), 67 fixed on `main` across
47 pull requests, 2 fixed upstream before they were reported. 53 came from
the first fuzzing pass (#274); 16 more surfaced as fixes removed the
generator's steering around known defects. ~60 reproducers were promoted to
permanent tests in `tests/` with host-proven baselines and checked
performance rows. Two proof-suite gate failures (#277, #280) were also fixed.
The bug record is [compiler-bugs.md](compiler-bugs.md); the root-cause
plan and per-cluster validation notes are [fix-plan.md](fix-plan.md).

### Infrastructure

- **#274** `scripts/clang-oracle-fuzz.py`: integer and float program
  generators, host-clang oracle, reduction and `--off known` steering;
  `scripts/run-known-bugs.py`; `tests/known-bugs/` reproducers.
- **#275** `clang-oracle-testing` skill and the root-cause `fix-plan.md`.
- **#221**, **#222** forced-regional MIR regression coverage; stale campaign
  assembly hashes repaired.
- Generators later gained `sizeof` emission, nested-block declarations,
  float update values and compound-assignment-through-pointer shapes as
  steers were removed (S2, T2, T3).

### Wrong code — nested-block (block-scope) local typing

Locals declared in nested blocks lost their declared type because block
declaration spans were replayed into the symbol table after the enclosing
statement had been lowered.

- **BUG-007** (#227) indirect call with a nested-block argument needing
  conversion failed `incorrect argument ABI type` or crashed — A1 #281.
- **BUG-025** (#245) `return x;` of a nested-block object was not converted
  to the return type — A1 #281.
- **BUG-031** (#251) nested-block 32-bit local stored to a 16-bit target
  wrote a garbage high byte — A1 #281.
- **BUG-008** (#228) negative constant assigned to a 32-bit nested-block
  local rejected — A2a #283, `tblkneg`.
- **BUG-004** (#225) 16-bit local initialised from `&&`/`||` then stored to a
  32-bit lvalue left garbage in the high word — A2b-1 #314, A2b-3 #329
  (byte logical PHI keeps its source type), `tlogblk`, `tlogbyte`.
- **BUG-015** (#235) nested-block pointer to a 32-bit object and nested-block
  `unsigned char` lost their declared type — A4 #328 (partial), A3 #331,
  `tptrblk`, `tblkbind`, `tblkcast`.
- **BUG-016** (#236) a local took the signedness of its initialiser instead
  of its declared type — E1 #306, `tinittyp`.
- **BUG-058** (#315) nested-block word initialised across signedness widened
  with the initialiser type into a 32-bit array — A3 #331, `tblkword`.
- **BUG-063** (#332) store through a nested-block pointer truncated a 32-bit
  value to 16 bits — T2 #335, `tblkstor`.
- **BUG-066** `sizeof` of a nested-block scalar local reported 2 — S2 #340,
  `tszblock`.
- **BUG-067** (#338) `sizeof` of an element, member or dereference of a
  nested-block local reported 2 — T3 #342, `tszderiv`.
- **BUG-068** (#339) `*q op= x` / `q[i] op= x` through a nested-block pointer
  to a 32-bit object added only the low word — T3 #342, `tptrcasg`.

### Wrong code — `++`/`--` and update expressions

- **BUG-003** (#224) `++`/`--` on a 32-bit array element computed in 16 bits
  and sign-extended — D1 #297, `tinc32a`.
- **BUG-044** (#264) `++`/`--` on a float was a no-op on a scalar and a
  16-bit integer increment on an element — D1 #297, `tflinc`.
- **BUG-021** (#241) `x++`/`x--` yielded the new value — D2a #300, `tpost16`.
- **BUG-023** (#243) `if (x++ > K) x = v;` left `x` as 1 when not taken —
  D2a #300, `tpostbr`.
- **BUG-010** (#230) cast or narrowing return of a 32-bit post-increment
  rejected — D2b #301, `tpost32`.
- **BUG-024** (#244) `if (x++)` in a loop tested the incremented value —
  D2c #305, `tpost8`.
- **BUG-041** (#261) value of a float `++`/`--` rejected — D2e-1 #321
  (update provenance, `tupdprov`), D2e-2 #337, `tfincval`.
- **BUG-056** (#299) float member-array update values negated as integers
  (latent) — D2e-2 #337, `tfnegupd`.
- **BUG-059** (#316) deferred float update arithmetic lacked the implicit
  integer-to-float conversion (latent) — D2e-2 #337, `tfupdcnv`,
  `tflbconv`.
- **BUG-014** (#234) 32-bit local with two definitions, incremented, then
  stored to a global: `missing direct MIR named-home offset` — B2a #292,
  `twincg`.
- **BUG-057** (#304) fused 32-bit increment used as an addition operand read
  a stale backend slot — M #311, `twincadd`.
- **BUG-009** (#229) `do … while (d < n)` nested in another loop never
  exited with 32-bit `d` — M #311, `tndwide`.

### Wrong code — PHI homes, liveness and stack slots

- **BUG-017** (#237) `x &= x` on an `unsigned short` parameter returned
  through a widening cast gave 0 — B3 #289, `tandself`.
- **BUG-026** (#246) `int d = a - b; return d * d;` read an unallocated
  slot — B3 #289, `tselfop`.
- **BUG-030** (#250) `for`-init declaration shared a slot with the enclosing
  block's locals — B1 #286, `tforhome`.
- **BUG-036** (#256) 32-bit local assigned in a loop and passed directly to
  a call kept its old value — B4 #293, `tlpcall`.
- **BUG-027** (#247), **BUG-038** (#258) local assigned in an `if … else`
  then updated lost the assignment when a loop / early return followed —
  C1 #313 (materialise word PHI edges for store-only fused adjustments).
- **BUG-029** (#249) 8-bit local and another variable assigned in opposite
  arms, then `(int)c + x` lost `x` — C2a #320, `tphiarm`.
- **BUG-037** (#257) `l = i;` copied a 32-bit loop counter after its
  increment — C2b #319, `twphicpy`.
- **BUG-052** (#272) folded `?:` on a 32-bit parameter accumulated into the
  parameter when it was assigned after the loop — C2c #317, `tphislot`.
- **BUG-019** (#239) unreachable nested-scope declaration after
  `return`/`break`/`goto` rejected — B2b #303, `tdeadblk`.
- **BUG-064** (#333) bounded recursion through a function pointer grew the
  stack without bound (promoted-local slot hole vs fused self-store) —
  T1 #334, `tslothol`.
- **BUG-020** (#240) after a `switch`, `v >= (param % k)` with a 32-bit
  parameter rejected — H #308, `tcmprhs`.
- **BUG-045** (#265) comparison against a negative float constant combined
  with another operand rejected — H #308, `tflncmp`.

### Wrong code — signedness, shifts and bit-fields

- **BUG-002** (#223) `unsigned char <<= unsigned char` not truncated to 8
  bits on return — E2-shift #309, `tbcshl`.
- **BUG-028** (#248) `>>=` on a `signed char` with an `unsigned int` count
  shifted logically — E2-shift #309, `tbcsar`.
- **BUG-013** (#233) unsigned bit-field not promoted to `int` as an operand
  — E2-bitfield #310, `tbfexpr`.
- **BUG-006** (#226) `/=`, `%=`, `>>=` on an unsigned bit-field with the top
  bit set stored the wrong value — E2c #324, `tbfcomp`.

### Valid C rejected — compound assignment and constant folding

- **BUG-011** (#231) compound assignment to a `char` array element inside a
  struct; **BUG-018** (#238) 32-bit value compound-assigned to a bit-field or
  16-bit array member element; **BUG-043** (#263) compound assignment to an
  integer lvalue with a float right operand — F #296.
- **BUG-012** (#232) unary `-`/`~` applied to `!` of a constant — G1 #295,
  `tconstun`.
- **BUG-033** (#253) constant divided by constant zero in unreachable code —
  G2 #302, `tconstdz`.
- **BUG-040** (#260) static float initialiser had to be a bare constant —
  G2 #302, `tflcini`.
- **BUG-042** (#262) float constant needing folding or a truth test rejected
  — G2 #302, `tflcnot`.

### Wrong code — floating point

- **BUG-046** (#266) `return` did not convert between `float` and a 32-bit
  integer — I1 #285, `tfltret`.
- **BUG-048** (#268) `(uint32_t)x` to float converted the signed value;
  **BUG-050** (#270) `(int32_t)(uint32_t)x` converted the unsigned value —
  I2 #287, `tflucast`, `tflscast`.
- **BUG-047** (#267) `f + (x * y)` dropped the product in several operand
  shapes — J #290, `tflmadd`.
- **BUG-049** (#269) `!f` was 0 for negative zero — J2 #291, `tflnot`.
- **BUG-055** (#294) variable-index store into a local float array in an
  `else` arm lost between nested loops — N #312, `tflidx`.
- **BUG-039** (#259) runtime `__fdiv` off by 1 ulp in about a fifth of cases
  — R1 #325 (normalise before rounding; helper 11 bytes smaller).
- **BUG-060** (#322) `%f` rounded exact IEEE bits to the wrong last decimal
  digit — F1 #330, `tfpround`.

### Wrong code — `sizeof`

- **BUG-061** (#323) `sizeof` ≥ 32768 sign-extended or divided as signed —
  S1 #326, `tsizhigh`.
- **BUG-062** (#327) small constant and runtime `sizeof` kept signed typing —
  S2 #340: `size_t` result in every form, with an AST proven-signedness
  demotion pass plus emitter and dccpeep recovery (param-only IX-frame
  elimination, `push hl / pop de` copies, exact `ex de,hl` liveness) so all
  baselines are non-regressing and 255 improve; `tsizesgn`.
- **BUG-069** (#341) `sizeof` of a multi-dimensional array struct member, or
  a partially indexed one, reported the innermost row — T4 #343, `tszmdim`.

### Runtime

- **BUG-065** a variadic function homed in IY corrupted its caller's IY
  (`va_list` frame shift) — S2 #340, `tvaiyhom`.
- **BUG-039**, **BUG-060** above are `DCCRTL.MAC` fixes.

### dccpeep

- **BUG-032** (#252) `small_const_incr_carry_skip` dropped `x + K` when the
  sum was then copied; **BUG-034** (#254) `cp8_to_bit3` turned `8 == v` into
  a bit-3 test; **BUG-035** (#255) `preserve_ix_word_signed_range` compared
  a 16-bit local with itself; **BUG-053** (#273) `pass_elim_dead_ix_stores`
  forgot an escape at an LS label — K #278.
- **BUG-054** (#288) byte local loaded from a global and used twice by a
  binary operator lost a live register — K2 #298, `tpephl`.

### Preprocessor and diagnostics

- **BUG-051** (#271) macro parameter named like a numeric suffix (`f`, `u`,
  `L`) substituted inside a constant — L #279, `tppsuf`.
- **BUG-022** (#242) a reused label name in a nested block hid
  `MIR emission is required` for later functions (exit 0, program hung) —
  M #311, `tlbreuse`.

### Fixed upstream before being reported

- **BUG-001** compound assignment of a 32-bit value to an 8/16-bit member,
  element or pointer target rejected; **BUG-005** nested-block divisor into a
  32-bit member zeroed the high word — upstream `34e35b3a`.

### Proof suite and CI

- **#277** compound-check-runner exact schedule rejected `main` in `cmpw4.c`
  after the bool-branch rewrite (cmp15ok/cmp4ok/cmp45ok controls failed);
  fingerprint and wave-45 audit baseline refreshed — T5 #344.
- **#280** `test_mir_compiler_mutations` intermittent exit 139 on macOS:
  the process supervisor spawned a `pwsh` runtime per supervised process;
  it now prefers `setsid`, then a direct `perl -MPOSIX` launcher — T5 #344.
- **#336** `ma.sh` runs `dccrtlstrip --strip-apps` like `dccmake` (fixes the
  m-posix CI job).
- Stale exact-schedule hashes found along the way were corrected rather than
  accepted: 39 campaign `BASELINE_SHA256` values (S2), five VLA schedules and
  the packed-record controls (T3).

### Withdrawn experiments (documented, not shipped)

- **A2c** #318 early lexical typing — changed 259 existing functions per
  mode and checked performance.
- **D2d** #307 float update provenance at binary consumers — dependent
  arithmetic still wrong; superseded by D2e-1/D2e-2.
- **A2**, **B**, **B2**, **D**, **E**, **G** — first root fix cleared only
  part of each cluster; split into the sub-clusters above.
