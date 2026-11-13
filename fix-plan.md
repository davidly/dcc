# Fix plan for the logged compiler bugs

[compiler-bugs.md](compiler-bugs.md) has fifty-one open bugs found by the
clang-oracle fuzzer (`.github/skills/clang-oracle-testing/SKILL.md`,
`docs/docs/en/appendix/06-differential-testing.md`). Many are one defect seen
from several angles. This plan groups them by *probable root cause* so that
one fix, made in the right place, closes several issues, and so that the
fuzzer can confirm it did.

The clusters are hypotheses drawn from the shapes that fail and the
neighbouring shapes that pass in each entry. They have not been confirmed in
the source. If the first fix in a cluster does not clear its siblings, stop
and report; do not force unrelated defects into one change.

## Rules

- One cluster per branch and pull request. Clusters A–D all touch MIR
  lowering and the homed/spilled emitters and will conflict; work them one
  at a time. I, K and L are isolated and can proceed in parallel.
- Do not change a generator's `KNOWN_BUGS` or a reproducer until the fix is
  in. The parked reproducers and the steering are the acceptance test.
- Acceptance for every bug in a cluster, from the skill's "Closing a logged
  bug" section:
  1. `python3 scripts/run-known-bugs.py` reports the cluster's reproducers
     `FIXED` (peep and nopeep) and every other reproducer still `fails`.
  2. The cluster's features are removed from `KNOWN_BUGS` and a 300-seed
     batch per affected generator with `--off known` is clean.
  3. `python3 ./scripts/runall.py -mode=full -extended` is `SUCCESS`, and
     **no existing row of `tests/perf_baselines.csv` changes**. Every
     program in `tests/` compiles correctly today, so a fix for code that
     is wrong must not alter the code emitted for code that is right. A
     changed baseline, in either direction, is a sign that the fix is wider
     than the defect; understand it before going on, and never move a
     baseline to absorb a regression.
  4. Reproducers are promoted to `tests/` (`tests/known-bugs/README.md`)
     as **new** unit tests with **new** `perf_baselines.csv` rows measured
     on the normal stack-check `runall.ps1` path, entries closed in
     `compiler-bugs.md`, issues closed from the commit.
- The goal is still concise, fast Z80. A fix is the narrowest change that
  makes the wrong shape right, not a general fallback to a slower or larger
  schedule. Compare peep and nopeep output for the reproducer before and
  after, and prefer a fix that emits for the bad shape what the compiler
  already emits for the neighbouring good shape (each entry lists them). If
  the only correct fix costs code in the new tests, note the cost in the
  pull request; it must never cost code in the existing ones.
- Run the regular MIR gates for compiler changes
  (`DCC_MIR_REQUIRE_COMPLETE=1 DCC_MIR_REQUIRE_EMIT=1` strict runall, debug
  metadata tests when statement or scope boundaries move; see
  `.github/skills/dcc-project/SKILL.md`).
- A fix that flips a reproducer outside its cluster is information: record
  which, and move that bug into the cluster in this file.

## Clusters

Priority follows the maintainer's own ranking of what ordinary code hits
(BUG-026, 044, 047, 030, 003, 013, 016, 025) and the number of issues a
fix would close.

### A. Nested-block locals lose their declared type

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-015 | #235 | Block pointer to 32-bit object: `*q *= k` in 16 bits, `c <= *q` signed; block `unsigned char` compared as `unsigned int`; `?:` of two block floats |
| BUG-025 | #245 | `return x;` of a block object not converted to 8- or 32-bit return type |
| BUG-031 | #251 | Block 32-bit local copied from a local, stored through a variable address: low byte only |
| BUG-008 | #228 | Negative constant assigned to a block 32-bit local: fatal |
| BUG-007 | #227 | Block or `for`-init local as a converted argument of an indirect call: bad ABI type |
| BUG-004 | #225 | `&&`/`||` assigned to a block 32-bit local leaves garbage high word (facet; the function-scope half is in E) |

Common control: every shape is right when the same local is declared at
function scope. Start in the MIR lowering of block-scope declarations and
the type recorded for their homes (`src/dcc/dcc_mir.c`, scope handling in
`dcc_func.c`): the block local appears to be typed from its initialiser or
from context rather than from its declaration. Expect this to be the largest
single win.

### B. Stack slot and home allocation

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-026 | #246 | `d = a - b; return d * d;` reads an unallocated stack slot |
| BUG-030 | #250 | C99 `for`-init declaration overlaps enclosing block's locals after an earlier sibling scope |
| BUG-014 | #234 | 32-bit local with two definitions, incremented, stored to a global: `missing direct MIR named-home offset` |
| BUG-036 | #256 | 32-bit local assigned in a loop and passed directly to a call: store dropped, reloaded from a slot never written |
| BUG-019 | #239 | Unreachable declaration after `return`/`break`/`goto` in a nested scope: fatal |

Common thread: a home or slot that exists in one view of the function and not
in another (allocator vs emitter, reachable vs unreachable scope, sibling
scope sizes). Start in frame layout and home assignment in
`dcc_mir_homed_cfg.c` / `dcc_mir_spilled_cfg.c` and the scope-size
accounting for `for`-init declarations. BUG-030 corrupts memory in ordinary
C99 code, so it goes first.

### C. Assignments lost across control-flow merges and loop-carried values

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-027 | #247 | Local assigned in `if … else` nested in an `if`, then updated: lost when a loop follows |
| BUG-038 | #258 | Same with no outer `if`: lost when an early-return `if` follows |
| BUG-023 | #243 | `if (x++ > K) x = v;` leaves `x` as 1 when not taken (local assigned from another type) |
| BUG-029 | #249 | 8-bit local and another variable assigned in opposite arms: `(int)c + x` loses `x` |
| BUG-037 | #257 | Copy of a 32-bit loop counter is taken after the increment |
| BUG-052 | #272 | Folded `?:` on a 32-bit parameter in a loop accumulates into the parameter |

The first four are a value merged from two arms that is never stored before
the update. BUG-037 and BUG-052 are a value read at the wrong point of the
loop. Both smell of the homed emitter's liveness/merge at join blocks
and of a temporary being homed in the variable it was computed from. Begin
with BUG-038, the smallest.

### D. Post-increment and post-decrement

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-003 | #224 | `++`/`--` on a 32-bit array element done in 16 bits |
| BUG-044 | #264 | Float `++`/`--`: no-op on a scalar, 16-bit integer increment on an array element |
| BUG-021 | #241 | `x++` yields the new value (16-bit local from a local array element) |
| BUG-024 | #244 | `if (x++)` in a loop tests the new value (8-bit local) |
| BUG-010 | #230 | Cast or narrowing return of a 32-bit post-increment: rejected |
| BUG-041 | #261 | Value of a float `++`/`--` rejected |

Two sub-problems: the operation is done at the wrong width for 32-bit and
float lvalues (003, 044), and the *value* of the expression is wrong or
unsupported (021, 024, 010, 041). Fixing the lowering of `++`/`--` into
"load, copy old value, widen, add, store" once for all widths should cover
both. BUG-044 is the maintainer's second priority.

### E. Signedness and integer promotion

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-016 | #236 | Local takes the signedness of its initialiser, not its declaration |
| BUG-013 | #233 | Unsigned bit-field not promoted to `int` as an operand |
| BUG-006 | #226 | `/=`, `%=`, `>>=` on an unsigned bit-field sign-extend it |
| BUG-028 | #248 | `signed char >>= unsigned int count` shifts logically |
| BUG-002 | #223 | `unsigned char <<= unsigned char` not truncated on return |
| BUG-017 | #237 | `x &= x` on a parameter, returned through a widening cast, gives 0 |
| BUG-004 | #225 | 16-bit local from `&&`/`||` stored to 32-bit lvalue: garbage high word |

Each is a result typed from the wrong operand: initialiser instead of
declaration, field width instead of promoted `int`, count instead of left
operand, 16-bit logical result instead of a widened value. Start with
BUG-016 and BUG-013 (maintainer priorities) in the expression typing /
conversion insertion that feeds MIR.

### F. Compound assignment to complex lvalues

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-011 | #231 | `op=` on an element of a byte array inside a struct: rejected |
| BUG-018 | #238 | `op=` of a 32-bit value to a bit-field or to an element of a 16-bit array member: rejected |
| BUG-043 | #263 | `op=` on an integer lvalue with a float right operand: rejected |

All `DCC-E1002`. Upstream `34e35b3a` handled plain members, elements and
pointer targets; these are the remaining lvalue kinds and the mixed-type
case. Likely one lowering switch that enumerates lvalue kinds.

### G. Constant expressions in the front end

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-012 | #232 | Unary `-`/`~` on `!`, `&&`, `||` of constants: rejected |
| BUG-033 | #253 | Constant divided by constant zero in dead code: rejected |
| BUG-040 | #260 | Static float initialiser must be a bare constant |
| BUG-042 | #262 | Float constant that must be folded or truth-tested: rejected |

Constant folding that stops at the first non-trivial form. BUG-033 needs the
fold to leave an unevaluated division in place rather than reject.

### H. Comparison with a compound right operand on a 32-bit or float parameter

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-020 | #240 | After `switch`/`if`+loop, `v REL param * / % k`, or `7.0f != (x0 + 28)`: `MIR emission is required` |
| BUG-045 | #265 | `x REL (-C OP y)` with a negative float constant: `MIR emission is required` |

Both are comparisons whose right side is an arithmetic expression over a
parameter, both fatal with no candidate able to emit, and both pass with the
operands swapped or the expression hoisted into a temporary. Look at how the
comparison selector accepts a non-leaf right operand.

### I. Float ↔ integer conversion

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-046 | #266 | `return` does not convert between float and 32-bit integer |
| BUG-048 | #268 | `(uint32_t)x` to float, `x` signed 32-bit: signed conversion |
| BUG-050 | #270 | `(int32_t)(uint32_t)x` to float, `x` signed 8/16-bit: unsigned conversion |

048 and 050 are mirror images: the conversion picks `__ltof`/`__ultof` from
the inner operand's type instead of the cast's. 046 is the missing implicit
conversion at `return`. Independent of A–E.

### J. Float arithmetic

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-047 | #267 | `f + (x * y)` drops the product (two factor-kind families) |
| BUG-049 | #269 | `!f` tests raw bits: `-0.0f` is true |
| BUG-039 | #259 | `__fdiv` is 1 ulp off in about a fifth of cases (runtime, `DCCRTL.MAC`) |

Three separate fixes; grouped only because they share the float test
harness. BUG-047 is the maintainer's third priority. BUG-039 is a runtime
change: keep it small, follow the existing `DCCRTL.MAC` idioms, and run
`scripts/rtl-iy-safety.py` and `scripts/audit-runtime-coverage.py`.

### K. dccpeep

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-032 | #252 | `x + K` (2–255) dropped when the sum is copied to another slot |
| BUG-034 | #254 | `8 == v` becomes a bit-3 test |
| BUG-035 | #255 | `a > x || x < k` after a stack struct member compares `x` with itself |
| BUG-053 | #273 | `pass_elim_dead_ix_stores` forgets an escaped address at its own `LS<n>:` label |

Four unrelated peephole rules, each with a precise description in its entry.
Confirm nopeep is right, fix the rule, and run
`pwsh ./scripts/run-dccpeep-tests.ps1`. Isolated from the compiler clusters.

### L. Preprocessor

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-051 | #271 | Macro parameter named like a constant's suffix is substituted inside the constant |

The number lexer stops before the suffix letters. One fix in
`src/dcc/dcc_preproc.c`. Isolated.

### M. Singletons

| Bug | Issue | Shape |
| --- | --- | --- |
| BUG-009 | #229 | Nested `do … while (d < n)` with 32-bit `d` never exits |
| BUG-022 | #242 | Reused label name in a nested block hides `MIR emission is required` for later functions |

BUG-022 is a diagnostic bug that turns a compile error into broken output,
so it matters more than its size suggests. Both are one-offs.

## Suggested order

1. **K, L** — isolated, cheap, four and one issues; good warm-up for the
   acceptance loop.
2. **A** — six issues, largest expected win.
3. **B** — BUG-030 corrupts memory; BUG-026 is the maintainer's first
   priority.
4. **D** — BUG-044 and BUG-003.
5. **E**, then **C** — both in the emitter's typing/merge paths; C is the
   least certain cluster.
6. **I, J, H, G, F, M** in any order.

## Tracking

| Cluster | Branch / PR | Status | Notes |
| --- | --- | --- | --- |
| A | | open | |
| B | | open | |
| C | | open | |
| D | | open | |
| E | | open | |
| F | | open | |
| G | | open | |
| H | | open | |
| I | | open | |
| J | | open | |
| K | | open | |
| L | | open | |
| M | | open | |

Update this table as clusters are taken, and move bugs between clusters when
a fix shows the hypothesis was wrong.
