# dcc compiler bugs

Defects found by differential testing against the host C compiler (Apple
clang). Every open entry reproduces with the shipped configuration: no
diagnostic environment variables, with and without `dccpeep`. Nothing here
has been fixed by this work; this file is the log.

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

## Summary

| ID | Issue | Kind | Defect |
|---|---|---|---|
| [BUG-002](#bug-002) | #223 | wrong result | `unsigned char` local `<<=` an `unsigned char` variable is not truncated on return |
| [BUG-003](#bug-003) | #224 | wrong result | `++`/`--` on a 32-bit array element is done in 16 bits |
| [BUG-004](#bug-004) | #225 | wrong result | 16-bit local set from `&&`/`\|\|`, stored to a 32-bit lvalue, leaves a garbage high word |
| [BUG-006](#bug-006) | #226 | wrong result | `/=`, `%=` and `>>=` on an unsigned bit-field whose top bit is set |
| [BUG-007](#bug-007) | #227 | rejects valid C, or crashes | Indirect call with a nested-block local argument that needs converting |
| [BUG-008](#bug-008) | #228 | rejects valid C | Negative constant assigned to a 32-bit nested-block local |
| [BUG-009](#bug-009) | #229 | hang | Nested `do … while (d < n)` with a 32-bit `d` never exits |
| [BUG-010](#bug-010) | #230 | rejects valid C | Cast or narrowing return of a 32-bit post-increment or post-decrement |
| [BUG-011](#bug-011) | #231 | rejects valid C | Compound assignment to an element of a byte array inside a struct |
| [BUG-012](#bug-012) | #232 | rejects valid C | Unary `-` or `~` applied to `!`, `&&` or `\|\|` of constants |
| [BUG-013](#bug-013) | #233 | wrong result | Unsigned bit-field is not promoted to `int` once it is an operand |
| [BUG-014](#bug-014) | #234 | rejects valid C | 32-bit local with two definitions, incremented, stored to a global |
| [BUG-015](#bug-015) | #235 | wrong result | Nested-block pointer to a 32-bit object, and nested-block `unsigned char`, lose their type |
| [BUG-016](#bug-016) | #236 | wrong result | A local takes the signedness of its initialiser, not of its declaration |
| [BUG-017](#bug-017) | #237 | wrong result | Parameter `x &= x` returned through a widening cast gives 0 |
| [BUG-018](#bug-018) | #238 | rejects valid C | 32-bit value compound-assigned to a bit-field or to an element of a 16-bit array member |
| [BUG-019](#bug-019) | #239 | rejects valid C | Unreachable nested-scope declaration after `return`, `break`, `goto` or `if (1) break;` |
| [BUG-020](#bug-020) | #240 | rejects valid C | After a `switch`, a variable compared with `param % k` (32-bit parameter) |
| [BUG-021](#bug-021) | #241 | wrong result | `x++`/`x--` yields the new value for a 16-bit local assigned from a local array element |
| [BUG-022](#bug-022) | #242 | silent bad code | A reused label name in a nested block hides `MIR emission is required` for later functions |
| [BUG-023](#bug-023) | #243 | wrong result | `if (x++ > K) x = v;` leaves `x` as 1 when not taken (local assigned from another type) |
| [BUG-024](#bug-024) | #244 | wrong result | `if (x++)` in a loop tests the incremented value (8-bit local, later `do … while`) |
| [BUG-025](#bug-025) | #245 | wrong result | `return x;` of a nested-block object is not converted to an 8-bit or 32-bit return type |
| [BUG-026](#bug-026) | #246 | wrong result | `d = a - b; return d * d;`: the second operand is read from an unallocated stack slot |
| [BUG-027](#bug-027) | #247 | wrong result | Local assigned in an `if … else` nested in an `if`, then updated: the assignment is lost when a loop follows |
| [BUG-028](#bug-028) | #248 | wrong result | `signed char` `>>=` an `unsigned int` count shifts logically (counts 9 to 15) |
| [BUG-029](#bug-029) | #249 | wrong result | 8-bit local and another variable assigned in opposite arms of an `if … else`: `(int)c + x` loses `x` |
| [BUG-030](#bug-030) | #250 | wrong result, or hang | A `for`-init declaration overlaps its enclosing block's locals on the stack (after an earlier sibling scope) |
| [BUG-031](#bug-031) | #251 | wrong result | Nested-block 32-bit local copied from another local, stored to a 16-bit lvalue: only the low byte is loaded |
| [BUG-032](#bug-032) | #252 | wrong result (`dccpeep` only) | `dccpeep` drops `x + K` (K from 2 to 255) when the sum is copied to another slot |
| [BUG-033](#bug-033) | #253 | rejects valid C | A constant divided by a constant zero, in code that never runs |
| [BUG-034](#bug-034) | #254 | wrong result (`dccpeep` only) | `dccpeep` turns `8 == v` into a test of bit 3 |
| [BUG-035](#bug-035) | #255 | wrong result (`dccpeep` only) | `dccpeep` compares a 16-bit local with itself in `a > x \|\| x < k` after a struct member on the stack |

Thirty-three are open: twenty-one give wrong results with no diagnostic
(BUG-030 can also hang; BUG-032, BUG-034 and BUG-035 need `dccpeep`), one
hangs, ten stop the compile (BUG-007 can instead compile and crash), and
one (BUG-022) turns a compile error into a broken program. BUG-026 is the
one most likely to be hit by ordinary code, then BUG-030 (it corrupts
memory in C99-style code), BUG-003, BUG-013, BUG-016 and BUG-025.

Two more were found and then fixed upstream before they were reported; see
[Fixed upstream](#fixed-upstream).

### History

Every open entry also reproduces with compilers built from `32d3e46d` (before
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

## BUG-007

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

## BUG-008

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

## BUG-014

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

## BUG-017

**An `unsigned short` parameter and-assigned with itself and returned through
a widening cast returns 0.**

```c
static int16_t f(unsigned short x) { x &= x; return (uint32_t)x; }   /* f(1) is 1; dcc 0 */
```

- With `dccpeep` a second form is also wrong: `x &= x; return (int32_t)x;`
  returns garbage for 40000 (2112 or 5184, depending on build options).
- Correct: `return x;`; `x |= x`; `x &= 3`; a local copy of the parameter.
- Unlikely in hand-written code; logged because the result is silently wrong.

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

## BUG-025

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

## BUG-026

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
- BUG-017 (`x &= x` on a parameter) also has one variable as both operands
  and may be the same defect.

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

## BUG-030

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

## BUG-031

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
- Not a regression: the v3.6.0 compiler (`20290235`) does the same.
- One more case of a nested-block local losing its declared width; see
  BUG-015 and BUG-025.

## BUG-032

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

## BUG-034

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
- The rewrite has been in the file since at least `3dd3b434` (2026-06-14).

## BUG-035

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
