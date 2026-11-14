# dcc compiler bugs

Defects found by differential testing against the host C compiler (Apple
clang). Every open entry reproduces with a shipped configuration: no
diagnostic environment variables (some require `dccpeep`). Fixed entries
and their historical diagnoses are retained separately below.

- **Compiler under test:** `83dc7a91` (upstream `main` on 2026-11-13).
- **Reproducers:** one program per open entry, `tests/known-bugs/bugNNN.c`,
  with the output it should print in `tests/known-bugs/baselines/bugNNN.txt`.
  Each baseline is the host compiler's output, and the program also runs
  clean under ASan and UBSan, so it is defined C whose result does not depend
  on `int` being 16 or 32 bits.
- **Check them:** `python3 scripts/run-known-bugs.py` builds each reproducer
  with clang and with dcc and reports `fails` or `FIXED`.
- **How they were found:** `scripts/clang-oracle-fuzz.py` (see
  `scripts/README.md`), then narrowed by hand with one-statement probes.
- **Reported:** each open entry is an issue on `davidly/dcc` (the Issue
  column below), with the full reproducer and expected and actual output.
- **Fixing them:** [fix-plan.md](fix-plan.md) groups the open entries by
  probable root cause, with the acceptance test for each fix.

## Summary

| ID | Issue | Kind | Defect |
|---|---|---|---|
| [BUG-002](#bug-002) | #223 | wrong result | `unsigned char` local `<<=` an `unsigned char` variable is not truncated on return |
| [BUG-003](#bug-003) | #224 | wrong result | `++`/`--` on a 32-bit array element is done in 16 bits |
| [BUG-004](#bug-004) | #225 | wrong result | 16-bit local set from `&&`/`\|\|`, stored to a 32-bit lvalue, leaves a garbage high word |
| [BUG-006](#bug-006) | #226 | wrong result | `/=`, `%=` and `>>=` on an unsigned bit-field whose top bit is set |
| [BUG-009](#bug-009) | #229 | hang | Nested `do … while (d < n)` with a 32-bit `d` never exits |
| [BUG-010](#bug-010) | #230 | rejects valid C | Cast or narrowing return of a 32-bit post-increment or post-decrement |
| [BUG-011](#bug-011) | #231 | rejects valid C | Compound assignment to an element of a byte array inside a struct |
| [BUG-012](#bug-012) | #232 | rejects valid C | Unary `-` or `~` applied to `!`, `&&` or `\|\|` of constants |
| [BUG-013](#bug-013) | #233 | wrong result | Unsigned bit-field is not promoted to `int` once it is an operand |
| [BUG-015](#bug-015) | #235 | wrong result | Nested-block pointer to a 32-bit object, and nested-block `unsigned char`, lose their type |
| [BUG-016](#bug-016) | #236 | wrong result | A local takes the signedness of its initialiser, not of its declaration |
| [BUG-018](#bug-018) | #238 | rejects valid C | 32-bit value compound-assigned to a bit-field or to an element of a 16-bit array member |
| [BUG-019](#bug-019) | #239 | rejects valid C | Unreachable nested-scope declaration after `return`, `break`, `goto` or `if (1) break;` |
| [BUG-020](#bug-020) | #240 | rejects valid C | After a `switch`, a variable compared with `param % k` (32-bit parameter) |
| [BUG-021](#bug-021) | #241 | wrong result | `x++`/`x--` yields the new value for a 16-bit local assigned from a local array element |
| [BUG-022](#bug-022) | #242 | silent bad code | A reused label name in a nested block hides `MIR emission is required` for later functions |
| [BUG-023](#bug-023) | #243 | wrong result | `if (x++ > K) x = v;` leaves `x` as 1 when not taken (local assigned from another type) |
| [BUG-024](#bug-024) | #244 | wrong result | `if (x++)` in a loop tests the incremented value (8-bit local, later `do … while`) |
| [BUG-027](#bug-027) | #247 | wrong result | Local assigned in an `if … else` nested in an `if`, then updated: the assignment is lost when a loop follows |
| [BUG-028](#bug-028) | #248 | wrong result | `signed char` `>>=` an `unsigned int` count shifts logically (counts 9 to 15) |
| [BUG-029](#bug-029) | #249 | wrong result | 8-bit local and another variable assigned in opposite arms of an `if … else`: `(int)c + x` loses `x` |
| [BUG-033](#bug-033) | #253 | rejects valid C | A constant divided by a constant zero, in code that never runs |
| [BUG-036](#bug-036) | #256 | wrong result | A 32-bit local assigned in a loop and passed directly to a call keeps its old value after the loop |
| [BUG-037](#bug-037) | #257 | wrong result | In a `for` or `while` loop with a 32-bit counter, `l = i;` copies the counter after its increment |
| [BUG-038](#bug-038) | #258 | wrong result | A 16-bit local assigned in one arm of an `if … else`, then updated, loses the assignment when an early-return `if` follows |
| [BUG-039](#bug-039) | #259 | wrong result | Float division (`__fdiv`) is 1 ulp off in about a fifth of cases, in both directions |
| [BUG-040](#bug-040) | #260 | compile error | A static-storage float initializer must be a bare constant: `(2.5f)`, `-(2.5f)` and `1.0f + 2.0f` are rejected |
| [BUG-041](#bug-041) | #261 | compile error | The value of a float `++`/`--` is rejected (`g = f++`, `if (f-- > x)`, `while (f-- > 0)`) |
| [BUG-042](#bug-042) | #262 | compile error | A float constant that must be folded or tested for truth is rejected: `-(-2.5f)`, `!2.5f`, `2.5f && n`, `if (2.3f)` |
| [BUG-043](#bug-043) | #263 | compile error | Compound assignment to an integer lvalue with a float right operand is rejected (`l *= f`, `n += 1.5f`) |
| [BUG-044](#bug-044) | #264 | wrong result | `++`/`--` on a float is a no-op on a scalar and a 16-bit integer increment on an array element |
| [BUG-045](#bug-045) | #265 | compile error | A comparison against a negative float constant combined with another operand (`x < -1.0f - n`) stops the compile with `MIR emission is required` |
| [BUG-049](#bug-049) | #269 | wrong result | `!f` is 0 when `f` is negative zero; `if (f)`, `f ? :` and `f == 0.0f` treat `-0.0f` as zero correctly |
| [BUG-052](#bug-052) | #272 | wrong result | In a loop, `a[1] = (0 ? K : x) - 1.0f` with a 32-bit parameter `x` assigned after the loop subtracts 1 from `x` on every iteration |
| [BUG-054](#bug-054) | #288 | wrong result | With `dccpeep`, a byte local loaded from a global and used twice by a binary operator loses a live register |

Thirty-five are open: twenty-one give wrong results with no diagnostic
(BUG-039 is in the runtime), one hangs, twelve stop the compile,
and one (BUG-022) turns a compile error into
a broken program. The most likely ordinary-code defects remaining are
BUG-044 (every float `++` is wrong), BUG-003,
BUG-013 and BUG-016.

Seventeen reported bugs are fixed: [three in cluster A1](#fixed-in-cluster-a1),
[one in cluster A2a](#fixed-in-cluster-a2a),
[one in cluster B1](#fixed-in-cluster-b1),
[one in cluster B2a](#fixed-in-cluster-b2a),
[two in cluster B3](#fixed-in-cluster-b3),
[four in cluster K](#fixed-in-cluster-k),
[one in cluster L](#fixed-in-cluster-l),
[one in cluster I1](#fixed-in-cluster-i1),
[two in cluster I2](#fixed-in-cluster-i2) and
[one in cluster J](#fixed-in-cluster-j). Two more
were found and then fixed upstream before they were reported; see
[Fixed upstream](#fixed-upstream).

### History

The original campaign entries (through BUG-053) also reproduce with
compilers built from `32d3e46d` (before
PRs #219 and #220), `5f733050` (tag 3.8.0) and `5bcac74e` (28 October), so
none is a regression from the last two weeks. Two are regressions since
v3.6.0 (`20290235`); a first-parent bisect gives:

- BUG-007: first bad `55fbb06f` (2026-09-06, "fix: correct MIR lowering and
  harden structural verification (#183)").
- BUG-008: first bad `d49e3d7f` (2026-09-14, "Merge #194: harden AST/MIR
  correctness, ABI and proof infrastructure").

The compile failures BUG-010, 011, 012 and 014 are already present in v3.6.0.
The wrong-result bugs and the hang also show with the v3.6.0 compiler, but it
was only tried with today's runtime, which is not a reliable pairing.

---

## BUG-002

**An `unsigned char` local shifted left by an `unsigned char` variable is not
truncated to 8 bits when returned from a function returning `unsigned char`.**

```c
static uint8_t a1[4] = { 200, 2, 3, 4 };
static uint8_t count = 5;
static uint8_t f(void) { uint8_t x = a1[0]; x <<= count; return x; }
/* f() is 0 (6400 truncated to 8 bits); dcc returns 6400 */
```

- Wrong whether the count is a global or a local `unsigned char`, and
  whether `x` starts from memory or a constant.
- Correct with a constant count, an `int16_t` count, `count & 7`,
  `x = x << count`, a `signed char x`, or a `uint16_t` return type.
- Selected emitter for `f`: `spilled-scalar-cfg`.

## BUG-003

**`++` and `--` on a 32-bit array element are carried out in 16 bits and the
result is sign-extended.** Values that stay inside −32768…32767 are
unaffected, which is why small counters work.

```c
static int32_t a[4] = { 0x80000000, 65535, 32767, 70000 };
++a[0];      /* want 80000001, dcc 00000001 */
a[1]++;      /* want 00010000, dcc 00000000 */
a[1]--;      /* (from 65535) want 0000fffe, dcc fffffffe */
```

- Wrong for `int32_t` and `uint32_t`, prefix and postfix, constant and
  variable index, and through a pointer (`++p[1]`).
- Correct: `a[1] += 1`, a 32-bit global scalar, a 32-bit struct member,
  `++*p`.

## BUG-004

**An unsigned 16-bit local initialised from `&&` or `||`, then stored to a
32-bit lvalue, leaves garbage in the high word.**

```c
uint16_t l = one || zero;
s.m1 = l;            /* uint32_t m1: want 00000001, dcc xxxx0001 (e.g. 09f90001) */
```

- Wrong with variable or constant operands, `&&` or `||`, and a 32-bit
  member, array element or global as the destination.
- The same garbage high word appears when `&&` or `||` is assigned straight
  to a 32-bit local declared in a nested block:
  `{ int32_t b = 0; b = ((--x) || 1); g = b; }` gives `00010001`. That is
  correct with the local at function scope, as an initialiser
  (`int32_t b = (--x) || 1;`), and for `!` or a comparison.
- Correct: `l = 1`; `l = one`; `s.m1 = one || zero` stored directly.
- Commit `f00e062e` ("Widen && and || results converted to long or float")
  fixed a neighbouring shape.

## BUG-006

**`/=`, `%=` and `>>=` on an unsigned bit-field store the wrong value when
the field's top bit is set.** The field is sign-extended from its own width,
as if it were a signed bit-field, before the operation.

```c
struct S { uint16_t m2 : 11; uint16_t m5 : 5; } s = { 1148, 18 };
s.m5 %= 7;         /* want 4,    dcc 2    */
s.m5 /= 21930;     /* want 0,    dcc 2    */
s.m2 %= 21930;     /* want 1148, dcc 296  */
s.m5 >>= 1;        /* want 9,    dcc 25   */
s.m2 >>= n;        /* n = 3: want 143, dcc 1935 */
```

- Every wrong value is what sign extension gives. 18 in 5 bits is `10010`,
  which sign-extends to `0xfff2` (65522): 65522 % 7 is 2, 65522 / 21930 is 2,
  and `0xfff2 >> 1` done arithmetically is `0xfff9`, 25 in 5 bits.
- Wrong for a field of any width, including all 16 bits
  (`uint16_t a : 16` holding `0x8421`: `a >>= 3` gives `0xf084`), in a
  global or local struct and through a pointer (`p->a >>= 3`), and when the
  result is used (`r = (s.a >>= 3)`).
- Correct: the same statements when the field's top bit is clear;
  `s.m5 = s.m5 % 7` and `s.m5 = s.m5 >> 1`; `+=`, `-=`, `*=`, `^=`, `<<=`
  (their low bits do not depend on the extension); a signed bit-field.

## BUG-009

**A `do … while (d < n)` loop nested inside another loop never exits when
`d` is 32 bits wide.** The comparison behaves as always true.

```c
int32_t i, d = 0, n = 0;
for (i = 0; i < 4; i++) {
    n = limit;
    do { ++d; } while (d < n);      /* never leaves */
}
```

With `&& passes < 100` added to the condition the function returns 103
instead of 6, which shows the comparison is the part that is wrong.

- Hangs for `int32_t` and `uint32_t` `d`; with `n` a local, a global, 16 or
  32 bits; with a 16- or 32-bit outer counter; with an outer `for` or `while`;
  with `++d` in the body or in the condition.
- Correct: the loop not nested; `while (d < n) { … }`; `if (d < n)`;
  `d != n`; `n` set from a constant; a 16-bit `d`.

## BUG-010

**A cast applied to a 32-bit post-increment or post-decrement is rejected,
and so is returning one from a function with a narrower return type.**
`DCC-E1002`, reported against the enclosing statement.

```c
r = (int16_t)(l2++);                   /* int32_t l2 */
r = (int32_t)(u2--);                   /* uint32_t u2 */
take((int8_t)(u2++));
uint8_t f(void) { int32_t l3 = 300; return l3++; }
```

- Accepted: `r = l2++` with a 32-bit or a 16-bit `r`; `(int16_t)(++l2)`;
  `(int16_t)(l1++)` on a 16-bit variable.
- Upstream commit `34e35b3a` fixed the neighbouring "cast of an assignment"
  rejection, which this entry originally included.

## BUG-011

**Compound assignment to an element of a `char`, `signed char` or
`unsigned char` array that is a struct member is rejected.** `DCC-E1002`.

```c
struct B { uint8_t b[4]; char name[4]; } s, *p;
s.b[2] ^= 1;
s.b[i] |= 0x20;
p->name[i] |= 0x80;
```

- Rejected for every operator tried (`^= += -= |= &= <<= *=`), constant and
  variable indexes, a struct variable, a pointer to struct, and an element of
  an array of structs.
- Accepted: `s.b[2] = s.b[2] ^ 1`; `s.b[2]++`; a byte array that is not a
  struct member; a 16-bit array member.

## BUG-012

**Unary `-` or `~` applied to `!`, `&&` or `||` of constants is rejected.**
`DCC-E1002`.

```c
r = -(!(-128));
r = ~(!15);
r = -(!0);
r = ~(1 || 2);
if (n > (~(1 && 2))) r = 1;
```

- Accepted: `-(!p)`, `~(!p)` and `~(p || 2)` with a variable;
  `r = !(-128); r = -r;`; `~(3 > 2)`; `!(1 || 2)`; `(1 || 2) + 1`.
- Two unary operators over a constant binary expression are rejected the
  same way: `~(-(7 >> 13))`, `-(~(7 >> 13))`, `-(-(7 >> 13))`,
  `~(+(7 >> 13))`, `~(-(8 - 8))`, `~(-(1 > 2))`. One unary operator
  (`~(7 >> 13)`), two over a literal (`~(-5)`) and a variable inside
  (`~(-(n >> 13))`) are accepted.

## BUG-013

**An unsigned bit-field narrower than `int` is not promoted to `int` once it
is an operand.** dcc promotes it for `bf - 1000` written directly (which is
what `tests/tbfprom.c` checks). But the result of unary `-` or `~`, of any
other binary operator, or of `?:` with a bit-field arm, is typed
`unsigned int`: it is zero-extended when widened and compared as unsigned.

```c
struct S { uint16_t m0 : 9; uint16_t m2 : 11; } s = { 444, 346 };
long r;
r = -s.m0;                       /* want -444, dcc 65092 */
r = (s.m2 + 1) - 1000;           /* want -653, dcc 64883 */
r = 1 - (sel ? s.m2 : 1);        /* want -345, dcc 65191 */
if ((sel ? -21931 : s.m1) > 1)   /* false; dcc takes the branch */
```

- Wrong for `- ~ | ^ & + * / % >> <<` and `?:`.
- Correct: `s.m2 - 1000`; `sel ? s.m2 : -1` assigned directly.

## BUG-015

**Locals declared in a nested block lose their declared type in further
shapes.** Through a nested-block pointer to a 32-bit object, `*q *= k` is
computed in 16 bits and `c <= *q` is compared as signed. A nested-block
`unsigned char` is compared as `unsigned int`.

```c
{ uint32_t *q = &a4[4]; *q *= 11; }          /* 21930 * 11: want 0003ae4e, dcc ffffae4e */
signed char c = -1;
{ uint32_t *q = &a4[0]; if (c <= *q) r = 7; } /* false (c converts to 0xffffffff); dcc takes it */
signed char l2 = -66;
{ unsigned char b = 1; if ((b & s.m5) > l2) return 1; }   /* true; dcc does not take it */
```

- Each is correct when the pointer or the `unsigned char` is declared at
  function scope.
- These are grouped because they share the nested-block condition; they may
  have separate causes. See also BUG-008.
- Beside a nested-block 32-bit local, another operand's cast is lost:
  `{ int32_t b = 0; out = b + ((int16_t)((uint16_t)a6[1])); }` with `a6[1]`
  holding -1 gives 65535, and `if (b >= ((int16_t)((uint16_t)a6[1])))` is
  not taken. Correct with `b` at function scope, or with one cast.
  It is not about `a6`: `{ int32_t b = 0; if (((int16_t)g16) > b) r = 1; }`
  with a `uint16_t g16` of 0x8000 takes the branch, as does
  `b + ((int16_t)g16)` (32768, not -32768) and
  `(int16_t)((uint16_t)(1 << (g1 & 15)))` with `g1` at 15 (seed 6006). The
  `(int16_t)` of an unsigned 16-bit operand is lost.
- A nested-block pointer to a `uint16_t` element with the top bit set is
  shifted and divided as signed (found by fuzzing at `83dc7a91`; seeds
  4245 and later): `{ uint16_t *q = &a2[1]; r = (*q) >> (g1 & 15); }` with
  `a2[1]` = 0x8001 and `g1` = 239 gives 255, not 1, and `(*q) / (g1 & 15)`
  gives 120, not 136. `*q >> 15` is wrong too; `r = (*q)` and `&` or `+`
  are right, and so is a pointer declared at function scope.
- A nested-block `uint16_t` that wraps is widened as signed (seed 6648):
  `{ uint16_t b = 0; --b; g9 = b; }` with a 32-bit `g9` gives -1, not 65535;
  `b--` and `(uint16_t)b` are wrong too, `b -= 1`, `b = 5; --b`, a store to a
  16-bit target and a function-scope `b` are right.
- Unary `-` of a nested-block `float` is computed on its low 16 bits as an
  integer (float generator, seed 30): `{ float b = 1.0f; fg = -b; }` stores
  0.0f, `{ float b = 1.1f; fg = -b; }` stores 13107.0f (the negated low word
  0xcccd of 0x3f8ccccd), `-b - b` is -1.0f and `-(-b)` is 0.0f. Right:
  `0.0f - b`, `-(b + z)`, `float c = -b;`, `-b` of a function-scope float.
- A nested-block integer local used directly as the operand of float
  arithmetic is not converted: its bits are taken as a float (so a small
  integer is a denormal, about zero). `{ int16_t b = 3; fg = b + 1.0f; }`
  gives 1.0f, `b * 2.0f` gives 0.0f, `b - 1.0f` gives -1.0f, and
  `b + c * 1.0f` with two block locals gives the integer 7 in the low word.
  The same for `int32_t` and `uint8_t` locals and for a C99 `for
  (int16_t i = 0; …)` loop variable (`fg = i + 1.0f`). `(float)b + 1.0f`,
  `(b + 0) * 1.0f` and `fg = b;` are right. Found by fuzzing (float
  generator, seeds 20 and 41).
- The same missing conversion in other float contexts: `fg *= b` gives 0
  and `fg += b` leaves `fg` unchanged; a store to a float array element,
  struct member or through a pointer (`fa[0] = b`, `s.m = b`, `*p = b`)
  stores the integer bits (`00000003`); `b == 3.0f` is 0; `fg > 0.0f ? b :
  fg` is `40400003`. A store to a plain float global or local (`fg = b`,
  `float l = b`), a float call argument, `(float)b` and `b < 3.5f` are
  right. Found by fuzzing (float generator, seeds 79 and 108).
- The mirror case: a nested-block *float* local in arithmetic with an
  integer operand (constant or variable) is wrong — `{ float b = 1.0f; fg
  = b * 2; }` gives 0.0f, `1 + b` gives 1.0f, `b + 2` gives 1.0f,
  `(int32_t)1 - b` gives -1.0f, `b - (uint32_t)1` gives 1.0f, `b * gl`
  (32-bit global) gives 0.0f, and `b -= (int32_t)1` leaves `80000001`.
  `b * 2.0f`, `b * (float)2` and a function-scope `b` are right. Found by
  fuzzing (float generator, 20 of 1200 seeds, e.g. 292, 303, 370, 381,
  435, 682, 689, 698, 722, 789, 1031, 1036, 1052, 1105, 1131, 1144, 1154,
  1248, 1328, 1428).
- The value of an assignment to a nested-block float behaves the same way:
  `{ float b = 1.0f; fg = -(b = 1.18f); }` stores `c523d000` (-2621.0f, the
  negated low word `0x0a3d` of `0x3f970a3d`), and `fg = (b = 1.0f) - 1;`
  stores 1.0f. `fg = (b = 1.18f);` and a function-scope `b` are right.
  Found by fuzzing (float generator, seeds 1490 and 1861).
- A `?:` whose arms are both nested-block floats behaves like the variable:
  `{ float b5 = 1.0f; g = 7 + (fg ? b5 : b5); }` stores 1 to an `int32_t`
  `g` (want 8), as does `fg ? b5 : b6` with a second block float `b6`, and
  storing to a float global. Right when either arm is a global
  (`fg ? b5 : fg`), a constant (`fg ? b5 : 3.0f`) or `b5 + 0.0f`. Found by
  fuzzing (float generator, seed 4750).

## BUG-016

**A local keeps the signedness of its initialiser instead of its declared
type.** It shows when the local is later widened to 32 bits or used in a
32-bit division.

```c
uint16_t l = gs;         /* int16_t gs = -31256 */
long r = l;              /* want 34280, dcc -31256 */

int16_t l = gu;          /* uint16_t gu = 50000 */
long r = l;              /* want -15536, dcc 50000 */

int32_t l = 1u | a3[1];  /* a3[1] = -3, so l = -3 */
r = x / l;               /* uint16_t x = 50000: want -16666, dcc 0 */
```

- Wrong when the initialiser is a global, a struct member, a parameter, a
  function result, or a negative constant (`unsigned short l = -100;` then
  `long r = l;` gives -100, not 65436).
- A cast to the declared type in the initialiser does not help when it only
  changes signedness: `unsigned short l = (uint16_t)p;` with `int16_t p`, or
  `int32_t l = (int32_t)u;` with `uint32_t u`, are still wrong.
- Correct when the value is assigned in a separate statement
  (`uint16_t l; l = gs;`), when the initialiser's cast also changes width
  (`(uint16_t)(int32_t)p`), for 8-bit locals, and when the local is only used
  at its own width (`l > 5`, `l / 2`).
- A separate assignment does not help in a nested block when the value is an
  unsigned constant: `{ short b = 0; b = (uint16_t)0xb840; out = (b % three); }`
  with a 32-bit `three` of 3 gives 2 (`47168 % 3`), not -2. Correct with `b`
  at function scope or a signed constant.
- With floats: `{ uint32_t b = (int32_t)1 - 78; fg = b; }` converts the
  signed value (`c29a0000` instead of `4f800000`), as does `float c = b;`
  in the block; and `{ uint16_t b = h(); fg = (float)b; }` with `h()`
  returning `int16_t -180` gives `-180.0f` instead of `65356.0f` (also in
  `8.1f > (float)b`). Found by fuzzing (float generator, seeds 456, 863,
  1203 and 1215).
- An `int32_t` local initialised from a `uint32_t` converts to float as
  unsigned even after it has been read correctly as an integer:
  `{ int32_t t = (int32_t)gu; fg = (float)t; }` with `gu = 0xffffffbd`
  gives `4f800000`, not `c2860000` (`-67.0f`); the same with `int32_t t =
  gu;` and `fg = t;`, and after `printf("%ld", (long)t)` has shown -67.
  Right: `int32_t t; t = (int32_t)gu;` or `(float)(t + 0)`. Found by
  fuzzing (float generator, seed 3792).

## BUG-018

**Compound assignment of a 32-bit value to a bit-field, or to an element of
a 16-bit array that is a struct member, is rejected.** `DCC-E1002`. Upstream
commit `34e35b3a` made the same statements compile for plain members, array
elements and pointer targets; these two kinds of target are what remains.

```c
struct S { uint16_t u : 9; int16_t s : 7; int16_t w[2]; } g, *p;
static uint32_t r = 5;
static int32_t big = 70003;
g.u |= r;            /* bit-field */
p->u -= r;
g.s >>= (r & 15);    /* 32-bit shift count */
g.w[1] += big;       /* element of an array member */
```

- Rejected for unsigned and signed bit-fields, directly, through a pointer
  and in an element of an array of structs; for `|= += -= ^= &= *= /= %= <<=
  >>=`; and for elements of `int16_t` and `uint16_t` array members with a
  constant or variable index.
- Accepted: a 16-bit right operand (`g.u |= r16`, `g.w[1] += r16`); a plain
  16-bit member (`g.plain += big`); a 32-bit array member (`g.l[1] += big`);
  `g.u = g.u | r`.
- Byte array members are rejected whatever the operand: see BUG-011.

## BUG-019

**A declaration in a nested scope that is unreachable because a `return`,
`break` or `goto` precedes it in the same block stops the compile.**
`dcc: fatal: MIR emission is required`. The declaration can be a block-local
or a `for` initialiser.

```c
if (sel) {
    return 1;
    { int16_t b = 1; low += b; }             /* unreachable block with a declaration */
}
if (sel) {
    return 2;
    for (uint8_t k = 3; k < 5; ++k) low++;   /* unreachable for-init declaration */
}
```

- Fails after `return`, `break` and `goto`, inside `if`, `else`, `for`,
  `while` and `switch` bodies.
- It also fails when the jump is conditional on a constant that is true,
  which leaves the same code unreachable: `if (7) break;`, `if (1) return 5;`,
  `if ((0 + 7) >= (-128)) continue;`, `if (0) { } else { break; }`.
- Accepted: the same dead code at function scope (not inside a nested
  block); dead code with no declaration (`return 1; { low += 1; }`); a
  `for` loop whose counter is declared at function scope; a conditional jump
  on a variable (`if (sel) continue;`, `if (1 || sel) break;`).
- The constant may be a folded cast: `if ((int16_t)(-1.0f)) { return 1; }`
  or `if ((int16_t)((int32_t)1000)) { return 1.0f; }` followed by `{ float
  b = 1.0f; }` in the same block is fatal. Found by fuzzing (float
  generator, seed 1957).
- Dead code like this is rare in hand-written programs, so this is low
  priority; it is still valid C.

## BUG-020

**After a `switch` statement, a relational comparison between a variable and
the 32-bit product, quotient or remainder of a 32-bit parameter stops the
compile.** `dcc: fatal: MIR emission is required`.

```c
static int32_t f(int32_t x)
{
    switch (sel) { case 5: hits++; }
    if (g16 >= (x % 115))        /* also x / 115, x * 115, x % g16 */
        return 1;
    return 0;
}
```

- Fails with `>=` and `<`; with the variable an 8-bit struct array element,
  an 8-, 16- or 32-bit global, or `(int32_t)g16`; as an `if` or `while`
  condition and as a value (`hits += (g16 >= (x % 115));`).
- Accepted: no `switch` before it (an `if` or a `for` loop before it is
  fine); `==`; the operands the other way round (`(x % 115) <= g16`); a
  constant on the left; `x + 115` or `x >> 3`; the parameter copied to a
  local first; the result held in a temporary; a 32-bit global in place of
  the parameter.
- Upstream commit `81937302` ("Don't pair a wide helper operand with an
  unpushed parameter or constant") fixed `if (x < n / x)`, which fails with
  the same message; this looks like a neighbouring shape.
- An `if … else` whose *then* branch contains a loop triggers it as well
  (the `switch` is not essential), and floats are affected: after
  `if (n) { while (n) n--; } else { }`, `if (fg2 < x0 * fg1)` with `x0` a
  float parameter and `fg1` a float global is fatal, as is
  `if (gl < xl * gl)` with 32-bit `xl`. `x0 * 2.0f`, `x0 - 2.0f`, `xl * 2`
  (a constant right factor), `fg1 * x0` (parameter on the right), the
  operands swapped (`x0 * fg1 > fg2`), no `else`, a loop in the `else`
  instead, or a non-loop `then` body are all fine. Found by fuzzing (float
  generator, seed 26).
- No loop or `switch` is needed either: a braced early return is enough.
  `int f(float x) { float l = -2.0f; if (x) { return 1; } return 1.0f >=
  (x - l); }` is fatal, with any condition (`if (n)`, `if (g)`, `if ((int)
  (-1.0f))`) and with `>=`, `>`, `==`, `+` or `*`. Fine: `if (x) return 1;`
  without braces, `return (x - l) <= 1.0f;` (constant on the right), `(l -
  x)` (parameter on the right), `x >= l`, both operands parameters, `l` a
  local assigned instead of returned, and the same with `int` or `long`
  operands. Found by fuzzing (float generator, seed 2017).
- The right operand of the float arithmetic can be an integer constant:
  after `if (n) { while (n5 > 0) n5 = 0; } else { }`, `if (7.0f != (x0 +
  28))` with a float parameter `x0` is fatal, as are `x0 - 28`, `x0 * 28`,
  `x0 / 28`, `x0 + (float)28`, `==`, `<`, an integer or float global on the
  left, the value assigned, and an `int` function. Fine: `x0 + 28.0f`, a
  float global in place of the parameter, the operands swapped (`(x0 + 28)
  != 7.0f`), `7.0f != x0`, no loop in the `then` body, or the comparison
  before the `if`. Found by fuzzing (float generator, seed 20063).

## BUG-021

**The value of a post-increment or post-decrement is the new value instead
of the old one**, for a signed 16-bit local that was assigned from an element
of a local unsigned 16-bit array, in a function that also has a `signed char`
local initialised from a narrowing cast.

```c
static uint8_t a0[8] = { 16, 76, 156, 127, 255, 188, 0, 57 };
static uint16_t f(void)
{
    signed char l1 = (int8_t)(1 - a0[1]);
    int16_t l3 = 1;
    uint16_t v6[2] = { 0xffde, 9029 };
    l3 = v6[1];
    return l3++;                 /* want 9029, dcc 9030 */
}
```

- Wrong for `return l3++`, `return l3--` (9028) and `r = l3++; return r;`,
  with the element read directly or through a pointer to the array.
- Correct: without `l1`, or with `l1` initialised from a constant, declared
  `int16_t` or `int32_t`, or initialised from `g32 - a0[1]`; with the array
  `static`; with `l3` assigned from a global; with `l3` declared `uint16_t`;
  `return ++l3`.
- The trigger is narrow and involves three unrelated-looking locals, so the
  cause is probably shared state (a slot or register), not the increment
  itself.

## BUG-022

**When a label name used in one function is reused inside a nested block of
a later function, dcc stops reporting `MIR emission is required` for the
functions after it.** It exits 0, and the program hangs or restarts in a
loop when the function it could not emit is called.

```c
static uint16_t first(void)  { L1:; return 1; }
static uint16_t second(void) { { L1:; } return 2; }       /* same name, nested block */
static int8_t third(void)    { { int32_t b = 0; b = -8305; low = (uint16_t)b; } return 3; }
```

`third` is a function dcc cannot emit (BUG-008). Without `first` and
`second` the compile stops with the fatal error. With them it succeeds and
the program fails at run time.

- Needs the same label name in both functions and the second one inside a
  nested block. With the second label renamed, or at function scope, the
  fatal error is reported as usual.
- Shown with the functions of BUG-008 and BUG-019. Functions that fail with
  `missing direct MIR named-home offset` (BUG-014) still stop the compile.
- This only matters while some function cannot be emitted, but then it turns
  a compile error into a silently broken program.

## BUG-023

**A 16-bit local that was assigned a value of a different type and is then
updated loses the assigned value when a branch or loop follows.** The update
is applied to 0 or to a stale value instead.

```c
static uint16_t f(int16_t n)
{
    uint16_t l;
    l = n;                  /* 300 */
    if (l++ > 400)          /* false */
        l = 0;
    return l;               /* want 301, dcc 1 */
}

short l = 0;
l = big;                    /* uint16_t big = 0xbeef, so l = -16657 */
l++;
for (i = 5; i > 0; i--) {
    if (zero) continue;
    l++;
}
g = l;                      /* want -16651, dcc 6 (or another unrelated value) */
```

- First shape: wrong when the local is `uint16_t` assigned from an `int16_t`
  parameter or global, from `(uint8_t)n` or from `n + 0`, and when it is
  `int16_t` assigned from a `uint16_t`; for `++` and `--`; for `>`, `>=`, `<`
  and `==`. Correct with `++l`, an `else` branch, a `while` instead of `if`,
  `l++;` as a separate statement, or the branch taken.
- Second shape: wrong for `l++`, `++l`, `l += 1`, `l = l + 1` and `l--`
  before the loop, with any update of `l` inside it. Correct without the
  update before the loop, or without the conditional `continue`.
- Both are correct when the local was assigned a value of its own type
  (`uint16_t` from `uint16_t`), and for 8-bit and 32-bit locals.
- The dependence on the assigned value's type suggests the same root as
  BUG-016.

## BUG-024

**`if (x++)` inside a loop tests the incremented value instead of the old
one**, for an 8-bit local that a later `do … while` loop also modifies.

```c
int8_t l = 0, i;
short d = 0;
for (i = 0; i < 3; i++) {
    if (l++)                /* l is 0, 1, 2: true twice; dcc: three times */
        hits++;
}
do { l--; } while (++d < 2);
```

- Wrong for `signed char` and `unsigned char`; for `if (l++)` and
  `if (l++ != 0)`; with the first loop a `for` or a `while`; with `l--`,
  `l -= 1` or `l++` in the later loop.
- Correct: a 16-bit local; no later `do … while` (a plain `l--;`, or a
  `while` loop, after the first loop is fine); the `if` not in a loop.
- BUG-021 is another case where a post-increment yields the new value.

## BUG-027

**A 16-bit local that is assigned in an `if … else` nested inside another
`if`, and then updated, loses the assignment when a loop follows.** The value
merged from the two arms is never stored; the update is applied to the old
value.

```c
int f(int n)
{
    int l = 0, i;
    if (one) {
        if (one) { l = n; } else { }
        l++;
    }
    for (i = 0; i < 2; i++)
        seen = l;
    return l;                /* f(9): want 10, dcc 1 */
}
```

dcc emits, after the inner `if … else`:

```
L15:
    ld l,(ix+4)        ; l = n: the value is left in HL
    ld h,(ix+5)
L5:
    inc (ix-2)         ; l++ in place on l's slot, which still holds 0
    jp nz, L16
    inc (ix-1)
```

- Wrong for `l++`, `++l`, `l--`, `l += 2` and `l = l + 1`; with the
  assignment in the `if` arm, the `else` arm or both
  (`if (c) l = n; else l = 2;`); with an empty or a non-empty `else`; when the
  outer `if` has an `else` of its own; for a local and for a parameter;
  assigned a variable or a constant; with constant or variable conditions.
- What follows can be a `for` or `while` loop that reads the local, or a
  conditional expression (`wide = (n ? x : 1);`).
- Correct: no outer `if`, or a plain block or a loop in its place; the inner
  `if` without an `else`; `l = c ? n : l;` or a `switch` in place of the
  inner `if … else`; nothing but `return l;` after it; an `unsigned char` or
  `long` local.
- The selected candidate is `spilled-phi-slot` (`DCC_MIR_COST_REPORT=1`).
- Not a regression: the v3.6.0 compiler (`20290235`) does the same.
- The symptom is BUG-023's (an update applied to a stale value), but nothing
  here is assigned from another type, so it is logged separately.

## BUG-028

**`>>=` on a signed 8-bit object with a count of type `unsigned int` shifts
logically instead of arithmetically.** The left operand is promoted to
`int`, which is signed, so the count's type must not matter.

```c
signed char g = -34;
unsigned n = 11;
g >>= n;                 /* want -1, dcc 31 */
g >>= 11u;               /* want -1, dcc 31 */
```

dcc sign-extends `g` to 16 bits and then shifts it with `srl h` / `rr l`
(`sra h` is needed), so zeros come in at the top: `0xffde >> 11` is `0x1f`.

- Wrong for a global, a local, an array element (`a[7] >>= 11u;`) and
  through a pointer; with the count a constant (`11u`, `(uint16_t)11`,
  `((uint16_t)20155) & 15`) or an `unsigned int` variable.
- The result only differs for counts 9 to 15: for 1 to 8 the low 8 bits of
  the logical and the arithmetic shift are the same.
- Correct: a count of type `int`, `unsigned char` or `unsigned long`;
  `g = g >> n;`; a 16-bit or 32-bit left operand; `<<=`.
- Not a regression: the v3.6.0 compiler (`20290235`) does the same.
- Low priority: shifting an 8-bit value right by more than 8 is unusual.

## BUG-029

**An 8-bit local is assigned in one arm of an `if … else` and another
variable in the other arm; when the two are then combined, with the 8-bit
local cast or negated, the other variable's value is lost.** On the path
that assigned the 8-bit local, the other operand becomes a copy of it (or 0).

```c
static uint16_t f(uint16_t x)
{
    int8_t l = 0;
    if (sel) { x = 1; } else { l = 100; }
    return (uint16_t)((int16_t)l - x);   /* sel 0, x 32767: want 32869, dcc 0 (100 - 100) */
}
```

dcc emits (`x` should be in DE at `L4`):

```
L8:                    ; sel != 0
    ld de,1            ; x = 1
    jp L4
L3:                    ; else
    ld hl,100          ; l = 100
    push hl
    pop de             ; ... and DE, the home of x, is overwritten with it
L4:
    or a
    sbc hl,de          ; l - x
```

- Wrong for `(int16_t)l - x`, `(int16_t)l + x`, `(uint8_t)l - x`, `-l - x`
  and `g = (int16_t)l; return g - x;`; for `int8_t`, `uint8_t` and `char`;
  when the other variable is a parameter, a 16-bit local or another 8-bit
  local; whichever arm assigns which; when one arm assigns both
  (`if (sel) { c = '-'; x = 5; } else { c = '+'; } return (int)c + x;`).
- Correct: no cast (`l - x`); `(int32_t)l - x`; the operands the other way
  round (`x - (int16_t)l`); `&`; a 16-bit `l`; an `if` with no `else`; both
  arms assigning only `l`; a `switch` or `?:` in place of the `if … else`.
- The selected emitter is `homed-scalar-cfg` (`DCC_MIR_COST_REPORT=1`).
- Not a regression: the v3.6.0 compiler (`20290235`) does the same.

## BUG-033

**A constant divided by a constant zero is rejected, even in code that never
runs.** `DCC-E1002: unsupported if condition or branch` (or
`unsupported AST statement`), a message that does not mention division.

```c
if (never)
    return 1000 / 0;
if (never)
    out = 7 % 0;
```

- The division is undefined only if it is evaluated, so a program that
  never reaches it is valid; clang compiles it with a warning.
- Accepted: `n / 0` and `n % 0` with a variable `n`.
- Low priority: nobody writes this on purpose. It can come out of macros
  (`SIZE / UNITS` in a branch guarded by `if (UNITS)`), and when it does the
  diagnostic points at the enclosing `if` rather than at the division.
- Already present in v3.6.0 (`20290235`).

## BUG-036

**A 32-bit local that is assigned inside a loop and passed directly to a call
keeps its old value after the loop.** The call gets the constant, but the
store to the local is dropped; the local is then reloaded from a stack slot
that was never written.

```c
int32_t l = 0;
uint16_t i;
for (i = 5; i != 0; i--) {
    l = 1;
    take32(l);               /* static uint32_t take32(int32_t x) */
}
return l;                    /* want 1, dcc 0 */
```

- Wrong for `for`, `while` and counted loops, a constant or a variable
  (`l = i;`), and with the call result stored or ignored; `l == 1` after the
  loop is false.
- Correct when the argument is an expression (`take32(l + 1)`), when the call
  does not take `l`, or when it takes a 16-bit parameter (`take16((int16_t)l)`).
- Wrong with `dcc-peep` on and off. Found by fuzzing at `83dc7a91` (seed
  6826); the reduced program had a `for`-init declaration, but a counter
  declared outside the loop fails the same.
- The `float` form is worse than stale: after `for (...) id((l0 = fg2));`,
  `{ l0 = fg2; id(l0); }`, `g = id((l0 = fg2));` or `l0 += fg2` inside the
  argument, `l0` is byte-scrambled (`7f74f6c5` for `c57f7400`) or stale.
  Right: no loop, a constant assigned (`l0 = 7.0f`), no initialiser on
  `l0`, `id((l0 = fg2) + 0.0f)`, or a `long` local in the same shape. Found
  by fuzzing (float generator, seed 3025).
- A compound assignment leaves the local equal to the constant operand:
  `while (n > 0) { l *= 2.0f; id(l); n = 0; }` with `l = 0.5f` returns
  2.0f (`l += 1.0f` returns 1.0f), for `float` and `long` and in `for` and
  `while` loops. Right when the call result is used (`fa[1] = id(l);`),
  when the argument is an expression (`id(l + 0.0f)`), with no call, or
  with no loop. Found by fuzzing (float generator, seed 3288).

## BUG-037

**In a `for` or `while` loop whose counter is 32 bits wide, a copy of the
counter taken in the body is stored after the increment.** The copy holds the
next value, so it is one step ahead. A `do … while`, a 16-bit counter and
straight-line code are correct.

```c
int32_t l = 0, i;
for (i = 2; i < 3; i += 1)
    l = i;
return l;                    /* want 2, dcc 3 */
```

dcc emits the increment first and then the copy, which reads the new `i`:

```
    inc (ix-6)         ; i += 1, in place
    ...
    ld l,(ix-6)        ; l = i
    ld h,(ix-5)
    ld e,(ix-8)
    ld d,(ix-7)
    ld (ix-2),l
```

- Wrong for `i += 1` and `i++` in a `for`, and for `i += 1` at the end of a
  `while` body (`while (i < 3) { l = i; i += 1; }`).
- Correct for `int16_t` locals, `do … while`, `i = i + 1` and `i += 2` in a
  `do … while`, and a copy outside a loop.
- Found by fuzzing at `83dc7a91` (seed 7581), with and without `dcc-peep`.

## BUG-038

**A 16-bit local that is assigned in one arm of an `if … else` and then
updated loses the assignment when an `if` that can return early follows.**
The update is applied to the old value. It is the loss in BUG-027 with no
outer `if` and no loop.

```c
unsigned short l = 0;
if (z == 0) {            /* volatile int16_t z = 1 */
} else {
    l = 1;
}
l -= 129;
if (!arr[1]) {           /* arr[1] is 5 */
    return 1;
}
sink = l;                /* unsigned char sink: want 128, dcc 127 */
return sink;
```

- Wrong for `l -= 129`, `l += 2` (want 3, dcc 2), an `int16_t` local, an
  assignment that is the condition of a nested `if` (`if (l = 1) { }`), and
  with and without `dcc-peep`.
- Correct without the update (`if (!arr[1]) return 1; sink = l;`).
- Without the early-return `if`, `dcc-peep` is still wrong and plain `dcc` is
  correct in a standalone program. In a larger file with other functions the
  same code gave different answers, so the result depends on what else is in
  the file.
- Found by fuzzing at `83dc7a91` (seed 9056). The reduced program had
  `if ((int32_t)0) { } else { if (l2 = 1) { } }`, which fails in every
  configuration including the forced MIR candidates.

## BUG-039

**Float division is off by one unit in the last place in about a fifth of
cases, in both directions.** Addition, subtraction, multiplication, the
comparisons and every integer/float conversion are bit-exact against IEEE
single precision (484 operand pairs each), so this is the rounding in the
runtime's `__fdiv` (`DCCRTL.MAC`), not a general precision limit.

```c
volatile float va = 1.0f, vb = 3.0f;
union { float f; uint32_t u; } b;
b.f = va / vb;      /* want 3eaaaaab, dcc 3eaaaaaa */
va = 10.0f;         /* 10/3: want 40555555, dcc 40555556 */
va = 1.0f; vb = 1.85f;  /* want 3f0a60dd, dcc 3f0a60de */
```

- 80 of 400 quotients over a 20-value table are wrong, all by exactly one
  ulp; 44 are too low and 36 too high, so it is not a missing round-up.
- Division by a power of two is exact.
- `1.0f / 3.0f` with constant operands is wrong too: dcc does not fold float
  division, it calls the runtime (`__fdf`, the fast-call entry of `__fdiv`),
  so every float quotient goes through the one routine.
- Also at v3.6.0 (`20290235`) with today's runtime.
- Found by hand probing (the float generator excludes `/` as `fdivround`).

## BUG-040

**The initializer of a float with static storage must be a bare floating
constant, optionally negated, or `(float)` of an integer constant.** A
parenthesised constant, a negated parenthesised constant or a constant
expression is rejected: `DCC-E1102 expected ';'` at file scope, `DCC-E0912
float initializer must be constant` for an element of a static array.

```c
static float a = (2.5f);              /* rejected */
static float b = -(2.5f);             /* rejected */
static float c = 1.0f + 2.0f;         /* rejected */
static float arr[2] = { (0.5f), 1.0f - 0.75f };   /* rejected */
static int ok = (-2) + 1 * 3;         /* accepted */
```

- Accepted: `-2.5f`, `(float)3`, every form as an automatic initializer
  (`float la = (2.5f);`) and as an assignment.
- The conformance table lists global initializers as supported without a
  float exception.
- Also at v3.6.0 (`20290235`).
- Found by fuzzing (float generator, seeds 1, 2, 4, 5, …): it rendered
  negative constants in parentheses.

## BUG-041

**The value of a float `++` or `--` expression is rejected.** `DCC-E1002`
("unsupported expression statement", "unsupported if condition or branch",
"unsupported while condition or body"), reported against the enclosing
statement, wherever the value is used.

```c
g = f++;                      /* rejected; float f, g */
g = ++f + 1.0f;
if (f-- > 3.0f) g = 7.0f;
if (--f) n = 1;
n = f++ > 1.0f;
g = (f-- > 10.0f) ? 1.0f : 2.0f;
while (f-- > 0.0f) g += 1.0f;
if (fa[1]++ > 1.0f) g = 9.0f;  /* array element, and a local float too */
```

- Accepted: `f++;` as a statement; the value of `f += 1.0f`; the value of
  an integer `++`/`--` in every one of these positions.
- The limitation is not documented.
- Also at v3.6.0 (`20290235`).
- Found by fuzzing (float generator, seed 3).

## BUG-042

**A floating constant that has to be folded or tested for truth is
rejected.** `DCC-E1002`, reported against the enclosing statement: unary `-`
of a negated or computed float constant, `!`, `&&` or `||` with a float
constant operand, and a float constant as the condition of `if` or `while`.

```c
g = -(-2.5f);              /* rejected; -(2.5f) is accepted */
g = -(1.0f + 2.0f);        /* rejected; -(1.0f + f) is accepted */
n = !255.0f;               /* rejected; !f is accepted */
n = 2.5f && 1;             /* rejected; 2.5f ? 1 : 2 is accepted */
if (2.3f) n = 7;           /* rejected; if (f), if (1.0f + f), if (1.0f > 2.0f) accepted */
while (1.0f) { break; }    /* rejected */
```

- Integer constants in every one of these positions are accepted, so it is
  the float constant folder that lacks `-` of a folded value and the truth
  test; compare BUG-012 (`-`/`~` of `!`, `&&`, `||` of integer constants).
- `&&`/`||` whose operands are *all* constant is also rejected when at least
  one operand is a float comparison: `if ((1.0f > 2.0f) && (0.0f == 0.0f))`,
  `if ((1.0f > 2.0f) && (1 == 1))`, `n = (1.0f > 2.0f) && 1`. Each
  comparison alone (`if (1.0f > 2.0f)`), a non-constant operand
  (`(1.0f > 2.0f) && n`), and the all-integer form (`(1 > 2) && (0 == 0)`)
  are accepted. Found by fuzzing (float generator, seed 30).
- Also at v3.6.0 (`20290235`).
- Found by fuzzing (float generator, seeds 3, 13, 16).

## BUG-043

**A compound assignment to an integer lvalue whose right operand is a float
is rejected.** `DCC-E1002 unsupported expression statement`, for every
integer width (8, 16 and 32 bits, signed and unsigned), every operator tried
(`+= -= *= /=`), a global or a local lvalue, and a variable or constant
right operand.

```c
int32_t l = 5; float f = 2.5f;
l *= f;                    /* rejected; want 12 */
n += f;                    /* int n */
u -= f;                    /* uint32_t u */
c8 += f;                   /* int8_t c8 */
l -= 1.5f;
```

- Accepted: the expanded form `l = l * f`; a float lvalue with an integer
  right operand (`f *= l`); `l %= 2`.
- Also at v3.6.0 (`20290235`).
- Found by fuzzing (float generator, seeds 7, 10, 25).

## BUG-044

**`++` and `--` on a float do the wrong thing with no diagnostic.** On a
scalar (a local, a global, a struct member, `*p`) the statement is a no-op.
On an array element (`fa[1]++`, `fa[i]--`, `p[3]++`, a local array too) the
element is treated as a 16-bit integer: the low word is incremented or
decremented and the result sign-extended over the high word.

```c
float l = 1.0f;
l++;            /* want 2.0f (40000000), dcc 1.0f (unchanged) */
fg++;           /* static float fg = 1.0f: unchanged */
s.m++;          /* unchanged */
(*p)++;         /* unchanged */
fa[1]++;        /* 2.0f: want 40400000, dcc 00000001 */
fa[2]--;        /* 3.0f: want 40000000, dcc ffffffff (a NaN) */
```

- Correct: `l += 1.0f`, `l = l + 1`.
- The value of a float `++` (`g = f++`) is rejected (BUG-041), so only the
  statement form reaches code generation, and it is always wrong.
- With and without `dcc-peep`.
- Also at v3.6.0 (`20290235`), tag 3.8.0 and `32d3e46d`.
- Found by fuzzing (float generator, seed 14: `la3[3]++; fg1 = la3[3];`).

## BUG-045

**A comparison whose right operand is a negative float constant combined
with a second operand stops the compile.** `dcc: fatal: MIR emission is
required`, with no line number. The shape is `x REL (-C OP y)` where `-C`
is a negative float constant (including `-0.0f`, `(-1.0f)` and `-(1.0f)`),
`OP` is `+ - * /` and `y` is an integer variable or constant of any width, a
cast, or a float global. The relation can be any of `< <= > >= == !=` and
the expression can be an `if`/`while` condition, a `?:` test or an
assigned value.

```c
if (x < -1.0f - n) r = 1;     /* fatal: n is int, int32_t, uint16_t, char... */
if (x < -1.0f - 2) r = 1;     /* fatal: even an integer constant */
if (x < -1.0f - fg) r = 1;    /* fatal: fg a float global */
if (x < -1.0f - (float)n) r = 1;   /* fatal */
if (x == -1.0f * n) r = 1;    /* fatal: so are + and / */
r = (x < -1.0f - n);          /* fatal */
```

- Accepted: a positive constant (`x < 1.0f - n`), the constant on the right
  (`x < n - 1.0f`, `x < -n - 1.0f`), the whole thing on the left
  (`-1.0f - n < x`), a float local/parameter/array element as `y`
  (`x < -1.0f - fl`), a wider `y` (`x < -1.0f - 2 * n`), or storing to a
  float first (`t = -1.0f - n; if (x < t)`). The bare value `-1.0f - n`
  outside a comparison is fine too.
- Also at v3.6.0 (`20290235`) and every later compiler checked.
- Found by fuzzing (float generator, seed 28:
  `(la3[3] - 100.0f) < ((-108.885f) - g1)`).
- The constant need not be negative: any comparison whose right operand is
  a float binop with a *constant* left operand is fatal — `1.0f <= (3.0f *
  fg2)`, `fg < (3.0f * fg2)`, `2 < (3.0f * fg2)`, `1.0f < (1174.3f + 1)`.
  `1.0f < (fg2 + 1)`, `1.0f < (fg2 * fg)`, `(3.0f * fg2) >= 1.0f` and
  `1.0f < -fg2` are accepted. Found by fuzzing (float generator, seeds 477
  and 1347).

## BUG-049

**`!f` is 0 when `f` is negative zero.** `!` on a float tests the raw bits,
so `-0.0f` (`80000000`) counts as true. The other truth tests are right:
`if (f)`, `f ? 1 : 0`, `f && 1`, `f || 0` and `f == 0.0f` all treat
`-0.0f` as zero.

```c
m0 = -((float)g5);         /* g5 == 0, so m0 is -0.0f */
n = !m0;                   /* want 1, dcc 0 */
if (!m0) puts("yes");      /* want yes, dcc no */
```

- `-0.0f` arises from negating a zero and from `0.0f * -1.0f`, so a
  `!` guard after a multiplication or negation can misfire.
- With and without `dcc-peep`.
- Also at v3.6.0 (`20290235`), tag 3.8.0 and `32d3e46d`.
- Found by fuzzing (float generator, seed 45).

## BUG-052

**In a loop, an expression with a folded `?:` on a 32-bit parameter is
accumulated into the parameter when the parameter is assigned after the
loop.** `fa[1] = (0 ? 2.0f : x) - 1.0f;` run three times stores `x - 3`
instead of `x - 1`: the parameter is treated as the expression's own
temporary and the subtraction is written back into it. In a `for` loop the
float's low word is overwritten by the counter as well.

```c
static void f1(float x) { int i; for (i = 0; i < 3; ++i) fa[1] = (0 ? 2.0f : x) - 1.0f; x = -1.0f; }
/* f1(-0.35f): want bfaccccd (-1.35f); dcc c0560002 (about -3.35f with the low word 0002) */
static void f3(float x) { int i; i = 0; while (i < 3) { fa[1] = (0 ? 2.0f : x) - 1.0f; i++; } x = g; }
/* dcc c0566666 (-3.35f) */
static void f4(long x) { int i; for (i = 0; i < 3; ++i) la[1] = (0 ? 2L : x) - 1L; x = -1L; }
/* f4(5): want 4; dcc 1 */
```

- Wrong with `0 ? K : x` and `1 ? x : K`, for `float` and `long`
  parameters, with a `for` or `while` loop, storing to an array element or
  a struct member, and whatever the parameter is assigned afterwards (a
  constant or a global).
- Right: an `int` parameter; a local copy of the parameter in place of it;
  `(x + 0.0f) - 1.0f` or `(float)x - 1.0f` (no `?:`); a store target that
  varies with the counter (`fa[i]`); a plain global target; no loop; and
  reading `x` after the loop (`fa[2] = x;` or `x = x + 1.0f;`) instead of
  only assigning it.
- With and without `dcc-peep`.
- Found by fuzzing (float generator, seed 2247).
- The test need not be constant: `for (...) l -= (c ? x : 1.0f); x = 1.0f;
  return l;` with `float c = 2.0f` returns 0 (want `46b78578`), as does
  `l = l - (c ? x : 1.0f)`, a `while` loop, `x = g;` after the loop, and
  the `long` form `l -= (ic ? x : 16777216L)` (returns 0, want 23493).
  `out[0] = (c ? x : 1.0f) - 1.0f;` in the loop is wrong as in the
  constant case. Right, exactly as above: no assignment to `x` after the
  loop, `x + 0.0f` as the arm, `x` in the else arm (`c ? 1.0f : x`), no
  `?:`, reading `x` after the loop, a local copy, or no loop. Found by
  fuzzing (float generator, seed 4294).

## BUG-054

**With `dccpeep`, a byte local loaded from a global and used twice by
a binary operator loses a live register.** The result can select
the wrong array element. Without `dccpeep` the output is correct.

```c
uint32_t sub(int8_t p)
{
    uint8_t a = 0, b = 0;
    uint32_t c = 0;
    a = s.c;                         /* a global struct's uint16_t member, 1 */
    b = a - a;
    c = a6[a1[b & 7] & 7];
    return c;                       /* want 127; peep gives 0xffffffa4 */
}
```

- `pass_defer_global_push_reload()` replaces `ld hl,(global) / push hl`
  and the matching `pop de` with a delayed `ld de,(global)`, but does not
  prove that HL is overwritten before its next use, including uses after
  that pop. After earlier spill cleanup, the right operand is still in HL
  from that same load; deleting it leaves HL stale. The subtraction becomes
  `stale_hl - global` instead of `global - global`.
- Confirmed for signed and unsigned byte locals, subtraction, addition and
  bitwise AND, a plain global or a global struct member as the source,
  with and without stack checking. The nested array lookup/32-bit result in the
  reproducer retains the failing schedule; a simple byte result or single
  byte-array lookup does not.
- Right: a parameter or a literal instead of the global load; every
  reproducer case with `dcc-peep=false`. A stale HL may coincidentally
  produce the correct result, so which operator visibly fails can change
  with link layout or optional runtime support.
- Found while verifying B3, integer generator seed 255 after removing
  BUG-026's broad `selfoperand` steer. Independently reproduced with an
  unchanged `972ce024` compiler and unchanged upstream `dccpeep`; it is not
  caused by B3's forwarded-operand repair. Parked, not fixed, in B3.
- Steered by `peepbyteself`: non-logical byte binary self-operands
  (including compound spellings), leaving word/long/float self-operands
  enabled for B3 coverage.

## Fixed in cluster B2a

BUG-014 now passes in peep and nopeep. The general spilled store tried to
load a wide value through the narrow loader before its full four-byte load.
A fused increment result lives directly in its named local home, which the
wide loader already resolves with the self-store proof; the preliminary
narrow loader instead looked for a plain load/object on the binary definition
and stopped the compile. Skip only that unnecessary preload for the proven
fused increment, preserving the existing in-place carry chain and wide load,
without adding spill slots or changing frames, allocation or selection.

The first fix clears only BUG-014: one `FIXED`, 35 `fails`, no unexpected
flips. B2 is therefore split into B2a (this fix) and B2b (BUG-019, still
parked). BUG-019 was traced separately: the metadata walk skips a dead nested
scope whose declaration placeholder MIR lowering retained. That unreplayed
placeholder and unresolved local leave no acceptable generated candidate;
none of its lowering or metadata code is changed here.

Integer `widelocalinc` is restored. Production seeds 1-300 give **213 ok,
85 INVALID, two target-limit-only, zero FAIL**, in peep/nopeep and stack/no-stack
configurations. A 3,000-seed steering proof is clean. The reproducer is
promoted as `twincg`, with carry, unsigned wraparound, assigned-add, loop,
untaken-arm and decrement/add-two coverage and a host/sanitizer-proven
baseline. After promotion all 35 remaining parked reproducers still fail.

Both MIR censuses preserve all **3,426 existing function rows byte-identically**;
only `twincg` adds nine functions. All **507 existing performance rows are
unchanged**; only its new row was measured through normal-stack PowerShell
runall. Strict full+extended stack and no-stack suites pass, as does the new
app's strict full-mode no-stack run. CMake MIR tests, debugger tests,
require-emit and focused metadata checks pass; the fixed shape has
release-identical `-gline` bytes and intact full-debug local/frame metadata
in both pipelines.

The full Python gate runs 312 tests with two approved pre-existing #277
failures (`test_mir_campaign_baselines`, `test_mir_exact_controls`) and two
existing skips. The #280 exit-139 mutation fixture passes in this run.
No expectation was fixed, skipped or refreshed; no historical compiler
checks or `prepush.ps1` run were performed.

| ID | Issue | Regression |
| --- | --- | --- |
| [BUG-014](#bug-014) | #234 | [twincg](tests/twincg.c) |

### BUG-014

**Fixed:** suppress the redundant narrow preload before a wide store of a
fused increment result. Regression: [twincg.c](tests/twincg.c),
[baseline](tests/baselines/twincg.txt).

**A 32-bit local that has two reaching definitions, is incremented by
one and is then stored to a global stops the compile.**
`dcc: fatal: missing direct MIR named-home offset`.

```c
int32_t l = 1;
if (sel) l = 5;
l++;                 /* also ++l, l += 1, l = l + 1, g = ++l */
g = l;
```

- The second definition can come from `if`, `switch`, a `goto` or a loop
  (`do { g = ++l; } while (…);`).
- It also fails for a `uint32_t` local stored to a `uint32_t` global.
- Accepted: `--l`; `l += 2`; storing to an array element or a local; storing
  through a conversion (`g = (int32_t)u;` with a `uint32_t u`); a local with a
  single definition.

## Fixed in cluster B3

BUG-017 and BUG-026 now pass with and without `dccpeep`. A repeated binary
operand could consume its just-forwarded register value for the first use,
then reload the second use from a spill home that had never been written
(and might not even be allocated). The emitter now recognizes that identity
before consuming either operand: copy HL into DE for words, or keep the
resident DE:HL after saving the first wide operand. No fallback, frame,
allocation or candidate-selection change is needed.

BUG-017 is the same defect, not a widening-cast typing fix: its unchanged
MIR is `binary v3 = v0,v0` followed by conversions, and both functions read
uninitialized `(ix-2..-1)` for the second operand before this repair.
Only these two existing reproducers flipped. The separately discovered
peep-only BUG-054 (#288), independently reproduced with unchanged current
main, remains parked and is steered by the byte-only `peepbyteself` feature.

`selfoperand`, `selfand`, and `fselfop` are restored. Final production seeds
1-300 with `--off known` give **213 ok, 85 INVALID, two target-limit-only,
zero FAIL** for integers and **267 ok, 33 INVALID, zero FAIL** for floats.
A separate 3,000-seed generator proof finds no remaining disabled shapes.
The reproducers are promoted as `tandself` and `tselfop`, including neighbouring
controls and the logged wide/float facets. Only their two new stack-check
performance rows are added: all 504 existing rows remain unchanged.
Both original-corpus censuses are identical across 3,394 existing functions;
the neighbouring other-operand and separated-statement assembly is unchanged
in both peep modes. Strict full+extended stack/no-stack, host/sanitizer,
line metadata and debugger-host checks pass; `-gline` retains release-identical
binaries and full `-g` runs give correct output in both modes.

After promotion all 37 remaining parked reproducers still fail; the final
censuses add only the 29 new test functions. Both strict full+extended gates
pass (545 applications), including the focused stack-check performance gate.
The Python suite runs 312 tests: two #277 failures have identical payloads
with unchanged current main; three unmodified mutation-harness errors remain
(two 180-second timeouts and a missing watchdog-child file), with two existing
skips. The timeouts also occurred in the unchanged-main fixture check. A
separate cleanup-case check fails on both clean current-main scripts and the
branch, though its nondeterministic symptom differs (8-second time budget
versus the missing child file). The full Python gate is therefore not green;
no expectation, skip or harness fix is included. The 25 focused integer,
generator and line-debug tests pass, as do all ten debugger-host tests.

| ID | Issue | Regression |
| --- | --- | --- |
| [BUG-017](#bug-017) | #237 | [tandself](tests/tandself.c) |
| [BUG-026](#bug-026) | #246 | [tselfop](tests/tselfop.c) |

### BUG-017

**Fixed:** duplicate the resident forwarded word instead of reloading its
uninitialized spill home. Regression: [tandself.c](tests/tandself.c),
[stdout](tests/baselines/tandself.txt).

**An `unsigned short` parameter and-assigned with itself and returned through
a widening cast returns 0.**

```c
static int16_t f(unsigned short x) { x &= x; return (uint32_t)x; }   /* f(1) is 1; dcc 0 */
```

- With `dccpeep` a second form is also wrong: `x &= x; return (int32_t)x;`
  returns garbage for 40000 (2112 or 5184, depending on build options).
- Correct: `return x;`; `x |= x`; `x &= 3`; a local copy of the parameter.
- Unlikely in hand-written code; logged because the result is silently wrong.

### BUG-026

**Fixed:** preserve the same just-forwarded word/wide value for both
operands of one instruction. Regression: [tselfop.c](tests/tselfop.c),
[stdout](tests/baselines/tselfop.txt).

**A variable that is assigned a computed value and is then, in the next
statement, both operands of one binary operator is read from a stack slot
the function never allocates.** The result is garbage.

```c
int sq(int a, int b)
{
    int d = a - b;
    return d * d;            /* sq(10, 1) is 81 and sq(20, 2) is 324; dcc returned 0 and 16578 */
}
```

dcc emits, for `sq`:

```
    push ix
    ld ix,0
    add ix,sp          ; no frame is allocated
    ...
    sbc hl,de          ; a - b
    push hl            ; first operand: forwarded
    ld l,(ix-10)       ; second operand: read from below the stack pointer
    ld h,(ix-9)
    ex de,hl
    pop hl
    call __mulu
```

- Wrong for a local with an initialiser (`int d = a - b;`) or assigned
  (`d = a - b;`), and for a parameter (`a = a ^ b; return a - a;`); assigned
  from an expression or from a global (`a = g; return a + a;`).
- Wrong for `+ - * & | ^ << >> == != < > <= >=`; in `return d * d;`,
  `g = d + d;`, `d = d * d;`, `return (d + d) * 2;`, `return d * d + 1;` and
  `if (d + d > 3)`; when the second operand is a copy
  (`int e = d; return d * e;`). A `long` local is wrong for `*`, `<<` and
  `>>`.
- `int`, `unsigned`, `short`, `int16_t` and `uint16_t` behave the same, in
  `static` and external functions.
- Correct: `d / d`, `d % d`, `d && d`, `d || d`; a different second operand
  (`d * a`, `d * (d + 1)`, `d + a + d`, `d + d + d`); another statement
  between the assignment and the use (`int d = a - b; calls++; return d * d;`);
  `int d = a;` or `int d = 7;` (nothing computed); a `char` local; a
  `register` local; another initialised local declared first
  (`int t = 1; int d = a - b; return d * d + t;`).
- The selected emitter is `spilled-scalar-cfg`, which reports `frame=0
  slots=0` for `sq` (`DCC_MIR_COST_REPORT=1`).
- Not a regression: the v3.6.0 compiler (`20290235`) emits the same code.
- BUG-017 (`x &= x` on a parameter) was confirmed to have the same
  forwarded-value double-consumption defect and is closed in B3 too.
- A nested-block `float` or `long` local initialised from a global or a
  converted integer has it too, and there the result is the first operand
  rather than garbage: `{ float b = fg; g = b + b; }` with `fg` 2.0f gives
  2.0f; `{ float b = c8; g = b + b; }` with `signed char c8` 3 gives 3.0f,
  `b - b` gives 3.0f and `b * b` is garbage; `{ long b = c8; g = b + b; }`
  is garbage. Right: `float b = 1.0f;`, `float b; b = c8;` (assigned, not
  initialised), `(b + 0.0f) + b`, a call between the two statements, and a
  function-scope `b`. Found by fuzzing (float generator, seed 1681).
- Also for a plain (function-scope) `float` local whose last assignment was
  a converted integer: `float l = 3.14159f; l = (uint16_t)0xdcc7;
  return (l * l) + (int32_t)1;` returns 1.0f (the product reads as 0) and
  `(l + l) + (int32_t)1` is wrong; `(l * l) + 1.0f`, `(l * l) + (int16_t)1`,
  `((int32_t)1) + (l * l)` and `l * l` alone are right, as is `l = 56519.0f`
  (the same value as a float constant) or dropping the dead `float l1`
  declaration next to `l`. Found by fuzzing (float generator, seed 3223).

## Fixed in cluster J

BUG-047 now passes with and without dccpeep. The wide register-forwarding
proof now declines a multiply suppressed by multiply-add fusion: the fused
add loads its addend before its factors, so the factor must retain a real
home rather than an invalid immediately-next-instruction handoff. Fusion
and its existing `__fmaf` schedule are retained. The neighboring correct
local-left-factor and product-left-addend controls are byte-identical in
both modes.

Before promotion, only BUG-047 flipped: **1 FIXED, 37 fails**. Both MIR
censuses retain all **3,394 existing functions** byte-for-byte, and strict
full+extended stack/no-stack regressions pass with all **504 existing
performance rows unchanged**. Only the new `tflmadd` row is measured and
added. Restoring `fintprodadd` to the float generator gives **269 ok,
31 INVALID, zero FAIL** on production seeds 1-300 with `--off known`.

BUG-049 and BUG-039 remain open: their correct fixes alter existing
function assembly and existing numeric stdout, respectively. Their
experiments were reverted under cluster J's acceptance rule; see
[the investigation notes](fix-plan.md#j-float-arithmetic).

| ID | Issue | Regression |
| --- | --- | --- |
| [BUG-047](#bug-047) | #267 | [tflmadd](tests/tflmadd.c) |

### BUG-047

**Fixed:** fused multiply-add factors retain a valid home until the fused
add actually loads them. Regression: [tflmadd.c](tests/tflmadd.c),
[stdout](tests/baselines/tflmadd.txt).

The following diagnosis describes the pre-fix behavior.

**`f + (x * y)` drops the product.** When the right operand of a float `+`
is a product, the result is just the left operand (whatever `f` is: global,
local, constant or parameter) in two families of factor kinds:

- the left factor `x` is an integer (a constant, `int`, `int32_t`,
  `uint16_t`, or `(float)128`), with any float right factor; or
- the right factor `y` is a local or parameter and `x` is a global, array
  element, struct member or sub-expression (`fv * l1`, `fa[1] * l1`,
  `s.m * p`, `fv * l1 * l1`). A few forms give other garbage instead of
  `f` (`(fv + 0.0f) * l1` is `-inf` in one context; the sum in `t()` of the
  reproducer is `5.0028`).

```c
fg + (2 * 3.0f)            /* want 7.0f, dcc 1.0f (fg) */
fg + (i * 3.0f)            /* 1.0f */
1.0f + (128 * 1.0f)        /* want 129.0f, dcc 1.0f */
r = fg; r += 2 * 3.0f;     /* 1.0f */
fg + (fv * l1)             /* l1 a local: want 10.0f, dcc 1.0f */
fg + (fv * p)              /* p a parameter: 1.0f */
```

- Right: `fg - (x * y)`, `fg * (x * y)`, `(x * y) + fg` (product on the
  left), `fg + (l1 * fv)` and `fg + (p * fv)` (local/parameter left
  factor), `fg + (fv * fv)`, `fg + (2.0f * fv)`, `fg + (float)i * 3.0f`,
  `fg + (x * y) * 1.0f`, `fg + (x + y)`, `fg + (x - y)`, `fg + (x / y)`.
- It is a per-function matter: the same expressions all compute correctly
  inside one 256-statement function, so the operands' register or stack
  placement decides it.
- With and without `dcc-peep`.
- Also at tag 3.8.0 (`5f733050`) and `32d3e46d`; v3.6.0 (`20290235`) cannot
  compile the reproducer at all (`MIR emission is required`).
- Found by fuzzing (float generator, seeds 7, 27, 31, 42 and 49).

---

## Fixed in cluster I1

BUG-046 now passes in peep and nopeep. Return admission now accepts long
expressions in float-returning functions and float expressions in
long-returning functions; existing MIR lowering already emits the required
conversion. The fixed program's complete assembly exactly matches an
explicit-return-cast control in both modes, with the control unchanged.

Before promotion, only BUG-046 flipped: one `FIXED`, 41 `fails`.
Both stack-mode censuses are identical across 3,366 existing functions;
strict full+extended regressions pass with all existing performance rows
unchanged. The float generator's `fretconv` steering entry was removed:
production seeds 1-300 with `--off known` give **265 ok, 35 INVALID,
zero FAIL**. The unchanged host baseline is promoted with one new measured
PowerShell stack-check performance row.

The float shape checker reports only disabled features (`prog.off`), as
its integer counterpart does. This directly coupled acceptance correction
keeps restored `fretconv` shapes eligible for execution rather than
incorrectly counting them as logged-shape INVALID.

| ID | Issue | Regression |
| --- | --- | --- |
| [BUG-046](#bug-046) | #266 | [tfltret](tests/tfltret.c) |

### BUG-046

**Fixed:** scalar return admission includes implicit long/float conversions.
Regression: [tfltret.c](tests/tfltret.c),
[stdout](tests/baselines/tfltret.txt).

The following diagnosis describes the pre-fix behavior.

**`return` does not convert between `float` and a 32-bit integer.** A
function returning `float` cannot `return` any `int32_t`/`uint32_t`
expression, and a function returning a 32-bit integer cannot `return` a
float expression: `DCC-E1002: unsupported return expression`. When the
`return` is inside an `if`, the error is reported against the `if`.

```c
static float a(int32_t x) { return x; }        /* rejected */
static float c(void) { return gl; }            /* rejected: int32_t global */
static float g(void) { return ff(); }          /* rejected: ff returns int32_t */
static float b(uint32_t x) { return x + 1; }   /* rejected */
static int32_t d(float x) { return x; }        /* rejected */
static uint32_t e(float x) { return x * 2.0f; } /* rejected */
```

- Accepted: `return (float)x;`, `float r = x; return r;`, `fg = x; return fg;`,
  and 8- and 16-bit integers in either direction (`float j(int16_t x)
  { return x; }`, `int16_t k(float x) { return x; }`).
- The same implicit conversion is accepted in assignments, initialisers and
  arguments, so only the return-value path lacks the long/float widening.
- Also at v3.6.0 (`20290235`) and every later compiler checked.
- Found by fuzzing (float generator, seeds 30 and 35:
  `return h0(...)` with `h0` returning `int32_t` in a `float` function).

## Fixed in cluster B1

BUG-030 now passes in peep and nopeep. A support probe could intern a
`for`-init scalar before preceding sibling scopes reserved their storage;
real declaration replay corrected the declaration table but not the MIR
object preferred by named-home resolution. Real `for`-init replay now repairs
that object only when its stale byte range overlaps another still-live local.
Expired for-init renames and unrelated declarations do not trigger repair, so
harmless existing homes and exact schedules remain unchanged.

Before promotion, BUG-030 alone flipped: one `FIXED`, 41 `fails`. The two
neighbouring controls are byte-identical in both modes. Both censuses retain
all 3,366 existing functions unchanged; strict full+extended stack-check and
no-stack-check suites pass with all 500 existing performance rows unchanged.
The integer generator's `armfordecl` steering entry was removed: production
seeds 1-300 finish with **216 ok, 83 INVALID, one target-limit-only, zero FAIL**.

The reproducer was promoted as `tforhome`, with extra 16-/32-bit counter
cases, a host/sanitizer-proven baseline, and one new measured stack-check
performance row. After promotion all 41 remaining parked reproducers still
fail. The full Python gate runs 307 tests with only the two approved #277
failures (failure payloads identical to unchanged main); the #280 mutation
fixture passes in this run and remains unmodified.

| ID | Issue | Regression |
| --- | --- | --- |
| [BUG-030](#bug-030) | #250 | [tforhome](tests/tforhome.c) |

### BUG-030

**Fixed:** overlapping probe-created for-init homes are reconciled during
real declaration replay. Regression: [tforhome.c](tests/tforhome.c),
[baseline](tests/baselines/tforhome.txt).

**A C99 `for` initialiser declaration overlaps the locals of its enclosing
block on the stack, when an earlier sibling scope also declares a local.**
The loop counter overwrites part of an array, a pointer, or a variable whose
address is taken. The sibling scopes can be the arms of an `if … else`, the
cases of a `switch`, or two blocks inside any other block.

```c
if (sel) {
    uint32_t a = 0;
    n += (uint16_t)a;
} else {
    uint16_t x[2] = { 0x1234, 0x5678 };
    for (uint8_t i = 5; i != 0; --i)
        n++;
    r = x[0] ^ x[1];         /* want 444c; dcc 124c: x[1] is now 0x0078 */
}
```

With a pointer in place of the array, dcc emits (the pointer `q` is at
`ix-6`/`ix-5`, and so is the counter):

```
    ld (ix-6),l        ; q = &bytes[0]
    ld (ix-5),h
    ...
    ld (ix-5),5        ; for (uint8_t i = 5; ...): i is also at ix-5
```

- Wrong when the `else` arm's locals are an array, two pointers, or a
  variable with its address taken; when the earlier arm's local is 8, 16 or
  32 bits wide, used or not; for an 8-bit or 16-bit counter; in the `else`
  arm of an `if` and in a later `case` of a `switch`.
- Equally wrong for two sibling blocks inside the body of a `for`, `while`
  or `if`, or inside a plain block:
  `for (…) { { uint32_t a = 0; … } { uint16_t x[2] = …; for (uint8_t i = 5; …) … } }`.
  Three generated programs that hang reduce to this form.
- With a pointer walk (`for (; q != end; ++q) { for (uint8_t i = 5; …) … }`)
  the counter clears the pointer's high byte on every pass and the loop never
  ends.
- Correct: the counter declared at the top of the block (`uint8_t i;`); a
  plain nested block in place of the `for` (`{ uint8_t c = 5; … }`); the
  earlier scope declaring nothing; two sibling blocks directly in the
  function body; the earlier local declared in the enclosing block itself;
  the two scopes in the other order; locals that do not need memory (plain
  `uint16_t x, y;`).
- Not a regression: the v3.6.0 compiler (`20290235`) does the same.
## Fixed in cluster I2

BUG-048 and BUG-050 now pass in peep and nopeep. Identity-cast elimination
preserves the result signedness of a same-width integer cast when its
consumer converts to float, just as it already did for widening consumers.
The long-to-float emitter now sees the right type and selects `__fulf` for
048 and `__flf` for 050. The parked programs' full assembly changes only
those calls and `extrn` placement; correctly typed controls are unchanged.

Before promotion, only these two bugs flipped: **2 FIXED, 40 fails**.
Both stack-mode censuses are identical across 3,366 existing functions;
strict full+extended regressions pass with all existing performance rows
unchanged. The float generator's shared `fu32cast` steering entry is
removed. Its shape checker now reports only disabled features, matching
the integer checker, so restored forms are actually executed rather than
silently marked INVALID. Corrected production seeds 1-300 with `--off known`
give **267 ok, 33 INVALID, zero FAIL**.

The original expected lines are preserved in the promoted tests, with
additional logged zero-add, folded/arithmetic and unsigned-global/local/
parameter facets validated against the sanitized host. Only two new
PowerShell stack-check performance rows are added. BUG-046 remains open
on this independent branch; its separate fix is [#285](https://github.com/davidly/dcc/pull/285).

| ID | Issue | Regression |
| --- | --- | --- |
| [BUG-048](#bug-048) | #268 | [tflucast](tests/tflucast.c) |
| [BUG-050](#bug-050) | #270 | [tflscast](tests/tflscast.c) |

### BUG-048

**Fixed:** float conversion retains the integer cast result's signedness.
Regression: [tflucast.c](tests/tflucast.c),
[stdout](tests/baselines/tflucast.txt).

The following diagnosis describes the pre-fix behavior.

**`(uint32_t)x` converted to float converts the signed value.** With `x` a
signed 32-bit expression, `(float)(uint32_t)x`, `m = (uint32_t)x;`,
`(uint32_t)x * 1.0f` and `0.0f + (uint32_t)x` all produce the signed
conversion: `-72` becomes `-72.0f` (`c2900000`) instead of
`4294967224.0f` (`4f800000`).

```c
m = (uint32_t)g2;          /* g2 = -72: want 4f800000, dcc c2900000 */
m = (uint32_t)(g2 + 1);    /* want 4f800000, dcc c28e0000 */
```

- Right: the cast stored in a `uint32_t` variable first (`u = (uint32_t)g2;
  m = u;`), a `uint32_t` global, `(uint32_t)gs` with a 16- or 8-bit signed
  operand, `(uint16_t)gs`, and integer uses of the cast (`(uint32_t)g2 / 2`
  is 2147483612).
- With and without `dcc-peep`.
- Also at v3.6.0 (`20290235`), tag 3.8.0 and `32d3e46d`.
- Found by fuzzing (float generator, seeds 12, 33, 56 and 60).


### BUG-050

**Fixed:** float conversion retains the integer cast result's signedness.
Regression: [tflscast.c](tests/tflscast.c),
[stdout](tests/baselines/tflscast.txt).

The following diagnosis describes the pre-fix behavior.

**`(int32_t)(uint32_t)x` converted to float converts the unsigned value.**
With `x` a signed 8- or 16-bit value, the outer `(int32_t)` cast is lost
when the result is converted to float: `-121` becomes `4294967175.0f`
(`4f800000`) instead of `-121.0f` (`c2f20000`). The mirror image of
BUG-048, where `(uint32_t)` of a signed 32-bit expression converts as
signed.

```c
fg = (int32_t)((uint32_t)g4);        /* int8_t g4 = -121: want c2f20000, dcc 4f800000 */
fg = (int32_t)((uint32_t)g5);        /* int16_t g5: same */
fg = (int32_t)((uint32_t)g4 + 1);    /* want c2f00000, dcc 4f800000 */
```

- Right: a 32-bit `x` (`(int32_t)(uint32_t)g6`), `(int32_t)g4` alone, and
  `(uint32_t)g4` alone (which is correctly `4f800000`).
- With and without `dcc-peep`.
- Found by fuzzing (float generator, seed 911).
- Also wrong when `x` is 32-bit arithmetic, including arithmetic that folds:
  `(float)((int32_t)(2147483647L + 1UL))` gives `4f000000`
  (2147483648.0f), not `cf000000`; `(float)((int32_t)(gl + 2147483647UL))`
  with `long gl` 7 is wrong too. Only a bare 32-bit variable,
  `(int32_t)(uint32_t)gl`, is right. Found by fuzzing (float generator, seed
  2036).
- A bare `uint32_t` variable with its top bit set is wrong too:
  `fg = (float)(int32_t)gu;` with `uint32_t gu = 0xffffffbd` (so -67)
  gives `4f800000` (4294967229.0f), not `c2860000`, for a global, a local
  or a parameter, with the cast explicit or implicit (`fg = (int32_t)gu;`);
  `g2 = (float)(int32_t)gu;` then stores -1 in the `int32_t`. Found by
  fuzzing (float generator, seed 3792).


## Fixed in cluster A2a

BUG-008 now passes in peep and nopeep. The spilled-CFG preflight excludes
`MIR_CONST` from object-displacement validation: declaration repair can
retain an object's metadata on a folded constant, whose `immediate` is a
32-bit payload, not an address offset. No lowering or emission schedule
changed.

Before promotion, only BUG-008 flipped: one `FIXED`, 42 `fails`. The fixed
signed and unsigned functions exactly match the function-scope controls'
assembly in both modes; positive, uninitialized and 16-bit controls are
unchanged. Both censuses are byte-identical across 3,363 existing functions.
Strict full+extended stack-check and no-stack-check runs pass; all 499
existing performance rows are unchanged.

The integer generator's `blockwideneg` steering entry was removed. Production
seeds 1-300 with `--off known` finished with **215 ok, 84 INVALID, one
target-limit-only, zero FAIL**. The unchanged host baseline was promoted with
one new measured PowerShell stack-check performance row.

| ID | Issue | Regression |
| --- | --- | --- |
| [BUG-008](#bug-008) | #228 | [tblkneg](tests/tblkneg.c) |

### BUG-008

**Fixed:** constant payloads are no longer checked as object displacements.
Regression: [tblkneg.c](tests/tblkneg.c),
[stdout](tests/baselines/tblkneg.txt).

The following diagnosis describes the pre-fix behavior.

**Assigning a negative constant to a 32-bit local declared in a nested block
stops the compile.** `dcc: fatal: MIR emission is required`.

```c
{ uint32_t b = 201; b = -3; }
```

- Fails for `int32_t` and `uint32_t`, with `-3` or `(int32_t)-3`. An
  `int32_t` local also fails with a constant whose value has the top bit set:
  `b = 0x80000000;`, `b = (uint32_t)0xfffffffe;`.
- Correct: the local at function scope; a positive constant; a 16-bit local;
  `uint32_t b; b = 0xfffffffe;`.
- Regression since v3.6.0; first-parent bisect: first bad `d49e3d7f`.

## Fixed in cluster A1

BUG-007, BUG-025 and BUG-031 now pass in peep and nopeep. Their unchanged
host baselines were promoted with new measured performance rows; all 43
remaining parked reproducers still fail. No existing performance row or
selected assembly changed: both stack modes compare identically across
3,333 existing functions.

Nested declarations are replayed after surrounding MIR is lowered. Return
and indirect-store conversion requirements now survive an unresolved load
without speculative instructions; they are fulfilled after declaration
types resolve. Fixed-prototype arguments retain their ABI target when
replay retypes their operands, leaving existing exact call schedules and
selection-time conversion materialization intact.

The integer generator's `ptrcallconv`, `blockret` and `blockwidecopy`
steering entries were removed. The production batch for seeds 1-300 with
`--off known` finished with **218 ok, 81 INVALID, one target-limit-only,
zero FAIL**. Initial failures at seeds 77 and 256 reduced to arithmetic
block-local arguments whose repaired wide type overwrote a word parameter
ABI; both facets are covered in the promoted ABI test and host MIR tests.

| ID | Issue | Regression |
| --- | --- | --- |
| [BUG-007](#bug-007) | #227 | [tblkabi](tests/tblkabi.c) |
| [BUG-025](#bug-025) | #245 | [tblkret](tests/tblkret.c) |
| [BUG-031](#bug-031) | #251 | [tblkcopy](tests/tblkcopy.c) |

### BUG-007

**Fixed:** a fixed parameter's prototype, not its repaired operand, owns
the argument ABI type. Regression: [tblkabi.c](tests/tblkabi.c),
[stdout](tests/baselines/tblkabi.txt), including the reduced seed-77/256
arithmetic-argument facets.

The following diagnosis describes the pre-fix behavior.

**A call through a function pointer whose argument is a local declared in a
nested block or a `for` initialiser, and whose type differs from the
parameter's, produces invalid MIR.** The verifier prints
`MIR f: instruction N has incorrect argument ABI type`. Then either the
compile stops with `MIR verification failed before required emission`, or,
in other surrounding code, dcc exits 0 and the program crashes at the call.

```c
static int16_t take(unsigned char x);
static int16_t (*take_p)(unsigned char) = take;
{ int32_t v = wide; (*take_p)(v); }                 /* narrowing */
for (int32_t i = 3; i != 0; --i) (*take_p)(i);      /* for-init local */
```

- Fails for narrowing and widening conversions, `(*p)(v)` and `p(v)`.
- Correct: the direct call `take(v)`; `v` declared at function scope; an
  explicit cast `(*take_p)((unsigned char)v)`; an argument of the parameter's
  own type.
- PR #219 fixed the direct-call form of this.
- Regression since v3.6.0; first-parent bisect: first bad `55fbb06f`.

### BUG-025

**Fixed:** unresolved return loads retain the function's conversion
requirement until their declaration types are known. Regression:
[tblkret.c](tests/tblkret.c), [stdout](tests/baselines/tblkret.txt).
The block return of 300 now emits `ld hl,44`, exactly matching the unchanged
function-scope control in peep and nopeep.

The following diagnosis describes the pre-fix behavior.

**`return x;`, where `x` is an object declared in a nested block, is not
converted to the function's return type.** To an 8-bit return type it is
not truncated (the caller receives all 16 bits); to a 32-bit return type it
is not widened (the high word is whatever was in the register).

```c
static uint8_t f(void)
{
    {
        int16_t b = 300;
        return b;            /* want 44 (300 truncated to 8 bits), dcc 300 */
    }
}
static int8_t g(void) { { uint8_t b = 200; return b; } }   /* want -56, dcc 200 */
static uint32_t w(void) { { int16_t b = 7; return b; } }   /* want 7, dcc e.g. 0x012c0007 */
```

- 8-bit return type (`uint8_t`, `int8_t`, plain `char`): wrong for a
  nested-block `int16_t`, `uint16_t`, `int32_t` (the low 16 bits are
  returned) and an 8-bit local of the other signedness.
- 32-bit return type (`int32_t`, `uint32_t`): wrong for a nested-block
  `int8_t`, `uint8_t`, `int16_t` and `uint16_t`. A nested-block `int32_t`
  is correct.
- Also wrong for a `static` local, an array element (`return b[0];`) and a
  struct member (`return s.m;`); for `return (b);`, `return b++;` and
  `return b = n;`.
- The nested scope can be a plain block, the body of an `if`, `while`,
  `do`, `for` or `switch`, or a `for` initialiser
  (`for (int16_t b = 300; …) return b;`).
- Correct: a local declared at function scope, even when the `return` is
  inside a nested block; a parameter; `return (uint8_t)b;`, `return b + 1;`,
  `return -b;`, `return +b;`, `return n ? b : 1;`, `return *p;`,
  `return (int32_t)b;`, `return b + 0;`; a 16-bit return type; copying to a
  function-scope local and returning that.
- Not a regression: the v3.6.0 compiler (`20290235`) does the same.
- Like BUG-007, BUG-008 and BUG-015 it depends on the object being declared
  in a nested block.

### BUG-031

**Fixed:** indirect stores retain the target conversion for an unresolved
block-local load, then materialize only the missing conversion.
Regression: [tblkcopy.c](tests/tblkcopy.c),
[stdout](tests/baselines/tblkcopy.txt).

The following diagnosis describes the pre-fix behavior.

**A 32-bit local declared in a nested block and initialised from a
function-scope 32-bit local is stored to a 16-bit lvalue with only its low
byte loaded.** The high byte stored is whatever was in the register.

```c
static void f(uint32_t x)              /* f(0x00030201) */
{
    uint32_t y = x;
    {
        uint32_t b = y;
        a2[1] = b;                     /* uint16_t a2[]: want 0201, dcc fe01 */
    }
}
```

dcc emits, for the store:

```
    ld l,(ix+6)        ; low byte of the value; H is not loaded
    ld (a2+2),hl
```

- Wrong when the destination is an array element (constant or variable
  index), a struct member or `*p`; for `uint32_t` and `int32_t`; in a plain
  block and in an `if` body.
- Correct: the local declared at function scope; initialised from a global,
  a constant or an expression (`x + 1`); declared without an initialiser and
  then assigned (`uint32_t b; b = y;`); stored with a cast (`a2[1] = (uint16_t)b;`) or as
  `b + 1`; stored to a plain global or to an 8-bit lvalue; a `for`
  initialiser (`for (uint32_t b = y; …)`).
- Also wrong: initialised with a constant and then assigned the other local,
  `{ int32_t b = 0; b = y; a2[1] = b; }` (found by fuzzing, seed 5089).
  A different type on the right, such as `uint32_t b = 0; b = y;` with an
  `int32_t y`, is correct.
- A constant initialiser is also wrong when the destination address is
  computed: `{ uint32_t b = 0x30201; a2[i] = b; }` with a variable index
  stores 0 (want `0201`), as do `*p = b` (`p` a global or local pointer)
  and `ps->m = b`. Right with a constant index, `s.m = b`, `(uint16_t)b`,
  `b + 1`, an initialiser that is a global or an expression, the local at
  function scope, or the block an `if` body. Found by fuzzing (seed 20149).
- Not a regression: the v3.6.0 compiler (`20290235`) does the same.
- One more case of a nested-block local losing its declared width; see
  BUG-015 and BUG-025.

## Fixed in cluster L

BUG-051 now passes in peep and nopeep. Its reproducer and unchanged host
baseline were promoted, and only its new measured performance row was added.
All 46 remaining parked reproducers still fail.

| ID | Issue | Regression |
| --- | --- | --- |
| [BUG-051](#bug-051) | #271 | [tppsuf](tests/tppsuf.c) |

### BUG-051

**Fixed:** both function-macro replacement and macro-argument/rescan walkers
copy complete preprocessing numbers before considering identifier expansion.
Suffixes, leading dots, exponent signs and identifier characters within a
pp-number are therefore never substituted as separate identifiers. Regression:
[tppsuf.c](tests/tppsuf.c), [stdout](tests/baselines/tppsuf.txt); focused script
tests also cover argument, object-macro and pasted-token rescans.

There was no BUG-051 feature in either generator's `KNOWN_BUGS`: it was found
while writing a probe, not in generated macro code. Both unchanged generators
passed 300-seed production batches with `--off known`.

The following diagnosis describes the pre-fix behavior.

**A function-like macro parameter whose name is the suffix of a numeric
constant in the replacement list is substituted inside that constant.** The
suffix letters are lexed as an identifier separate from the number, so
`f`, `u`, `l`, `L`, `U`, `F` and their combinations (`ul`) were all open to
substitution.

```c
#define T(f) g = 9.0f; f(1)
#define U(u) n = 10u; u
T(a1);        /* expands to `g = 9.0a1; a1(1);`: error DCC-E1003 malformed statement near 'g' */
U(n += 2;)    /* expands to `n = 10n += 2;; n += 2;` */
```

- A suffix that is not a parameter name is fine, and so are `1e5` with a
  parameter `e` and `0x10` with a parameter `x`: only the trailing
  integer/float suffix letters are detached.
- The result is normally a compile error; a replacement that happens to
  stay valid would silently change meaning.
- Found while writing a probe for the float fuzzer's seed 2247 (a `T(f)`
  test macro with `9.0f` in its body).

## Fixed in cluster K

The maintainer approved four independent peephole fixes together in
`fix/cluster-k-peephole`, including the single-source closed-program contract
needed to preserve the existing `ttt` schedule. The following diagnoses
describe the pre-fix behavior; all four reproducers now pass in peep and nopeep.
Their stdout baselines were promoted unchanged, and only four new measured
performance rows were added. The remaining 47 parked reproducers still fail.

| ID | Issue | Regression |
| --- | --- | --- |
| [BUG-032](#bug-032) | #252 | [tpeepsum](tests/tpeepsum.c) |
| [BUG-034](#bug-034) | #254 | [tpeeq8](tests/tpeeq8.c) |
| [BUG-035](#bug-035) | #255 | [tpeerng](tests/tpeerng.c) |
| [BUG-053](#bug-053) | #273 | [tpeesc](tests/tpeesc.c) |

### BUG-032

**Fixed:** `hl_relied_on_after()` now tracks H/L reads and overwrites
separately and protects pending register-ABI call inputs. Regression:
[tpeepsum.c](tests/tpeepsum.c), [stdout](tests/baselines/tpeepsum.txt).

**With `dccpeep`, a 16-bit local plus a constant from 2 to 255, copied to
another slot, loses the sum.** The copy stores whatever HL held before the
add. Without `dccpeep` the program is correct.

```c
static uint32_t g6 = 256;
static uint8_t g8 = 69;
static uint16_t f(int16_t x)
{
    x = x;
    return (g6 <= 1) ? g8 : (uint16_t)(x + 200);   /* f(1): want 201, with dccpeep 0 */
}
```

dcc's output for the false arm, and what `dccpeep` makes of it:

```
    ld l,(ix-2)            |    ld a,(ix-2) ; peep: small_const_incr_carry_skip
    ld h,(ix-1)            |    add a,200
    ld de,200              |    ld (ix-2),a
    add hl,de              |    jr nc,LI0
    ld (ix-2),l            |    inc (ix-1)
    ld (ix-1),h            | LI0:
    ld l,(ix-2)            |
    ld h,(ix-1)            |
    ld (ix-4),l            |    ld (ix-4),l      ; HL was never loaded: it still
    ld (ix-3),h            |    ld (ix-3),h      ; holds the result of __leu
```

- The pass's guard, `hl_relied_on_after()` in `src/dccpeep/dccpeep.c`, looks
  for the text `hl` in the lines that follow. `ld (ix-4),l` and
  `ld (ix-3),h` read H and L separately, so they are not seen.
- Wrong for constants 200 and 255, in `?:` and in
  `if (…) r = g8; else r = (uint16_t)(x + 200);`, with `x = x;` or `x += 0;`
  before it.
- Correct with `dccpeep`: constants 1, 256, 2000 and -56; a 16-bit
  comparison in the condition (no helper call, so HL happens to be right);
  without `x = x;`.
- The sum can also be the operand of a helper call, not only a copy:
  `while ((w++) < 1) sink = (l += 225) / 121;` with `int16_t l = 1` gives 0,
  not 1, with `dccpeep`, because `ld l,(ix-2)` / `ld h,(ix-1)` before
  `call __q2s` is removed (the same for `%`). Without the loop it is correct.
- The pass dates from `bff0d144` (2026-07-26), before v3.6.0.

### BUG-034

**Fixed:** the bit-3 rewrite requires a local 0..8 proof or an immutable
recursive-byte proof under the explicit `-closed-program` contract. The latter
checks all root arguments, +1 recursive edges, and the terminal CFG path,
without matching application or function names. Regression:
[tpeeq8.c](tests/tpeeq8.c), [stdout](tests/baselines/tpeeq8.txt).

**With `dccpeep`, `8 == v` can become a test of bit 3**, so 9, 12, 47 and
every other value with that bit set compares equal to 8. Without `dccpeep`
the program is correct.

```c
static uint8_t g8 = 47;
static int16_t f(uint16_t x)
{
    if (8 == (uint16_t)g8) {     /* false; with dccpeep it is taken */
        x = 100;
    }
    return x;
}
```

`dccpeep` output for the test (`g8`, zero-extended, is in `ix-2`/`ix-1`):

```
    bit 3,(ix-2) ; peep: cp8_to_bit3
    jr z,L3
    ld a,(ix-1)
    or a
    jr nz,L3
```

- The rewrite is in `pass_and1_ix_to_bit()` in `src/dccpeep/dccpeep.c`:
  `ld a,(ix+K) / cp 8 / jp nz,L` becomes `bit 3,(ix+K) / jp z,L`. Its
  comment says "Valid for depth values 0-8: bit3 is set only at depth=8",
  but nothing checks that the value is in that range.
- Wrong for `8 == (uint16_t)g8` and `8 != (uint16_t)g8` in an `if` with no
  `else`, for the values 9, 12 and 47.
- Correct with `dccpeep`: the values 7, 8 and 16; a comparison with 16;
  `c == 8` on a `char` parameter or local in an `if`, a loop, a `switch` or
  as a value (the simple forms do not produce the three-instruction pattern,
  so `if (c == '\b')` is safe as far as these probes go).
- Also wrong when the 16-bit operand is a signed bit-field read through a
  cast (seed 7161): `8 == (int16_t)s.m` with a 5-bit `m` of 9, 12 or 15,
  and `uint32_t l = (8 == (int16_t)s.m) ? 0 : 1;`. A plain `int16_t` global
  and `8 == s.m` without the cast are right.
- The rewrite has been in the file since at least `3dd3b434` (2026-06-14).

### BUG-035

**Fixed:** the range-preservation prefix rejects stack effects and unrelated
control flow before inserting a saved operand; stack-neutral `ex de,hl`
remains eligible. Regression:
[tpeerng.c](tests/tpeerng.c), [stdout](tests/baselines/tpeerng.txt).

**With `dccpeep`, a test of a 16-bit local as the right operand, after a
struct member that is already on the stack, compares the local with itself.**
Without `dccpeep` the program is correct.

```c
struct S1 { int16_t m0; uint16_t m1; uint8_t m2; };
static struct S1 s1 = { 0, 292, 255 };     /* x1 = s1 */
static int32_t a3[4] = { 32766, 32768, 147, -1682854718 };

static uint8_t f(struct S1 x1)
{
    uint16_t l2 = 0;
    uint32_t i8;
    l2 = (uint32_t)1550294082;             /* 0x9c42 */
    for (i8 = 3; i8 > 1; i8 -= 1) {
        if (((uint16_t)x1.m0 > l2) || (l2 < (uint16_t)1))
            a3[1] = 1;                     /* 0 > 0x9c42 and 0x9c42 < 1 are false: want 32768, dccpeep 1 */
        switch ((uint32_t)1) { case 100: break; default: break; case 10: break; }
    }
    l2 = (uint16_t)1;
    return 1;
}
```

What `dccpeep` makes of the first test (`pop hl` / `pop de` read the same word):

```
    push hl                ; x1.m0
    ld l,(ix-2)
    ld h,(ix-1)
    push hl ; peep: preserve_ix_word_signed_range   (inserted)
    push hl                ; the original push of l2
    pop de
    pop hl                 ; was x1.m0, is now l2
    ex de,hl
    or a
    sbc hl,de              ; l2 - l2
```

- `pass_preserve_ix_word_across_signed_range()` in `src/dccpeep/dccpeep.c`
  is written for `x < low || x > high`, where the word is the left operand of
  the `sbc hl,de`. It scans from the `ld l,(ix-N)` / `ld h,(ix-M)` pair to
  the next `sbc hl,de` / `jp c` and accepts a `push hl` / `pop de` /
  `pop hl` shuffle in between, so it also fires when the word is the right
  operand.
- Wrong for the loop with a `switch` on a constant or on the counter, and
  with a `uint8_t` or `uint32_t` counter. Correct: the same function without
  the `switch`, without the assignment after the loop, or with one test
  instead of two.
- Reproduces with `5f733050`, `32d3e46d` and `5bcac74e`.

### BUG-053

**Fixed:** dead-store elimination keeps escaped-frame state across every
compiler/optimizer `L...` label and across returns, resetting it only at
genuine segment boundaries. Regression:
[tpeesc.c](tests/tpeesc.c), [stdout](tests/baselines/tpeesc.txt).

**With `dccpeep`, a 32-bit parameter whose address is held in a pointer,
assigned after an empty counted loop and then read through the pointer,
returns its old value.** `pass_elim_dead_ix_stores` records that `&x0`
escaped (`push ix / pop hl / ld de,4 / add hl,de`) and protects stores to
`(ix+4..7)` from the escape onwards, but any label that is not `L<digits>:`
is taken as a new function and resets that protection. The empty loop's
signed compare is rewritten by `fold_signed_cmp_via_bytes` with an `LS<n>:`
label, so at `ret` the four `ld (ix+4..7)` stores of `x0 = 1.0f` look
dead and are deleted; `*p` then reads the caller's argument.

```c
static float f(float x0)
{
    float *p = &x0;
    {
        int16_t i;
        for (i = 0; i < 2; ++i) {}
    }
    x0 = 1.0f;
    return *p;              /* f(0.3f): want 3f800000 (1.0f); dccpeep 3e99999a (0.3f) */
}
```

- Wrong for `float` and `int32_t` parameters, with the loop in a plain
  block, under `if ((int32_t)12)`, `if (1)` or `if (g == 0.0f)`.
- Right: an `int16_t` parameter; no loop; a loop with a body (`s += i;`)
  or a `while` loop (no `LS` label is produced); `return x0` instead of
  `*p`; `*p = 1.0f; return x0;`.
- Right without `dccpeep` (`dcc-peep=false`). Any peep pass that
  introduces an `LS`, `LI` or `LR` label between the escape and the store
  should expose the same thing.
- Found by fuzzing (float generator, seed 3036).

---

## Fixed upstream

Found at `81937302` and fixed by upstream commit `34e35b3a` ("Give
assignments, compound assignments and ++/-- their stored values") before
they were reported. Their reproducers now pass.

- **BUG-001:** compound assignment of a 32-bit value to an 8- or 16-bit
  member, array element or pointer target (`p->w ^= r`, `a8[i] += r`,
  `*q <<= r`) was rejected with `DCC-E1002`. Bit-field targets and elements
  of array members are still rejected: BUG-018.
- **BUG-005:** `{ uint16_t b = 1; s.m3 /= b; }` with a 32-bit member `m3` and
  a nested-block divisor zeroed the member's high word.

## Not yet confirmed

- **`a1` is 5.7 times slower with the regional emitter forced.** With
  `DCC_MIR_PREFER_REGIONAL=1`, at `83dc7a91`, `runall.py -apps a1 -mode=full`
  no longer hangs and the output is correct, but it is a performance
  regression: peep 10,329,123 -> 67,385,816 cycles and nopeep 12,162,933 ->
  69,161,297 cycles, with `.COM` 19,072 -> 22,400 and 21,120 -> 24,576 bytes.
  The `6502fun` scenario runs well inside 30 s. That variable is
  diagnostic-only, so this is latent at most, and it has not been
  investigated further.
- **A module larger than 64K is not diagnosed as such.** Without `dccpeep`,
  a 1,100-line generated program whose code passes 64K assembles with
  `No Fatal error(s)` and then fails in the linker with
  `l80c: module T: code range 2853..2854 out of bounds (segment size 2853)`:
  the assembler's location counter appears to wrap. With `dccpeep` the same
  program gets the clear `program too large: extends past 0xFFFF`. This is a
  confusing message for a real limit, not wrong code, and is not reported.
