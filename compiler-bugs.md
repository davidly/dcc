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
| [BUG-006](#bug-006) | #226 | wrong result | `/=` and `%=` on an unsigned bit-field |
| [BUG-007](#bug-007) | #227 | rejects valid C, or crashes | Indirect call with a nested-block local argument that needs converting |
| [BUG-008](#bug-008) | #228 | rejects valid C | Negative constant assigned to a 32-bit nested-block local |
| [BUG-009](#bug-009) | #229 | hang | Nested `do … while (d < n)` with a 32-bit `d` never exits |
| [BUG-010](#bug-010) | #230 | rejects valid C | Cast or narrowing return of a 32-bit post-increment or post-decrement |
| [BUG-011](#bug-011) | #231 | rejects valid C | Compound assignment to an element of a byte array inside a struct |
| [BUG-012](#bug-012) | #232 | rejects valid C | Unary `-` or `~` applied to `!` of a constant |
| [BUG-013](#bug-013) | #233 | wrong result | Unsigned bit-field is not promoted to `int` once it is an operand |
| [BUG-014](#bug-014) | #234 | rejects valid C | 32-bit local with two definitions, incremented, stored to a global |
| [BUG-015](#bug-015) | #235 | wrong result | Nested-block pointer to a 32-bit object, and nested-block `unsigned char`, lose their type |
| [BUG-016](#bug-016) | #236 | wrong result | A local takes the signedness of its initialiser, not of its declaration |
| [BUG-017](#bug-017) | #237 | wrong result | Parameter `x &= x` returned through a widening cast gives 0 |
| [BUG-018](#bug-018) | #238 | rejects valid C | 32-bit value compound-assigned to a bit-field or to an element of a 16-bit array member |
| [BUG-019](#bug-019) | #239 | rejects valid C | Unreachable nested-scope declaration after `return`, `break` or `goto` |
| [BUG-020](#bug-020) | #240 | rejects valid C | After a `switch`, a variable compared with `param % k` (32-bit parameter) |
| [BUG-021](#bug-021) | #241 | wrong result | `x++`/`x--` yields the new value for a 16-bit local assigned from a local array element |

Nineteen are open: nine give wrong results with no diagnostic, one hangs,
and nine stop the compile (BUG-007 can instead compile and crash). BUG-003,
BUG-013 and BUG-016 are the ones most likely to be hit by ordinary code.

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
- Correct: `l = 1`; `l = one`; `s.m1 = one || zero` stored directly.
- Commit `f00e062e` ("Widen && and || results converted to long or float")
  fixed a neighbouring shape.

## BUG-006

**`/=` and `%=` on an unsigned bit-field store the wrong value.**

```c
struct S { uint16_t m2 : 11; uint16_t m5 : 5; } s = { 1148, 18 };
s.m5 %= 7;         /* want 4,    dcc 2   */
s.m5 /= 21930;     /* want 0,    dcc 2   */
s.m2 %= 21930;     /* want 1148, dcc 296 */
```

- Correct: `s.m5 = s.m5 % 7`; `+=`; `%=` on a signed bit-field.

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

**Unary `-` or `~` applied to `!` of a constant is rejected.** `DCC-E1002`.

```c
r = -(!(-128));
r = ~(!15);
r = -(!0);
```

- Accepted: `-(!p)` and `~(!p)` with a variable; `r = !(-128); r = -r;`.

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

**A signed 32-bit local that has two reaching definitions, is incremented by
one and is then stored to a global stops the compile.**
`dcc: fatal: missing direct MIR named-home offset`.

```c
int32_t l = 1;
if (sel) l = 5;
l++;                 /* also ++l, l += 1, l = l + 1, g = ++l */
g = l;
```

- The second definition can come from `if`, `switch` or a `goto`.
- Accepted: `--l`; `l += 2`; a `uint32_t` local; storing to an array element
  or a local; a local with a single definition.

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
- Accepted: the same dead code at function scope (not inside a nested
  block); dead code with no declaration (`return 1; { low += 1; }`); a
  `for` loop whose counter is declared at function scope; a conditional jump
  (`if (sel) continue;`).
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

- **`a1` hangs with the regional emitter forced.** With
  `DCC_MIR_PREFER_REGIONAL=1` the `a1` test's `6502fun` scenario
  (`-x -a:400 6502FUN.HEX`) runs past 30 s in both peep modes. That variable
  is diagnostic-only, so this is latent at most, and it has not been
  investigated. It was observed at `aa9b2f1c` and not rechecked since.
