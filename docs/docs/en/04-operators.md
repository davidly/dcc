# Operators

The DCC C Compiler supports the full C89 operator set. The only things to watch are the
behaviours that follow from the 16-bit `int` / 32-bit `long` model.

## The full set

- **Arithmetic:** `+` `-` `*` `/` `%` (and unary `+` / `-`).
- **Bitwise:** `&` `|` `^` `~` `<<` `>>`.
- **Logical / relational:** `&&` `||` `!`, `==` `!=` `<` `<=` `>` `>=`.
- **Assignment:** `=` and the compound forms `+=` `-=` `*=` `/=` `%=` `&=` `|=`
  `^=` `<<=` `>>=`.
- **Increment / decrement:** `++` `--` (prefix and postfix).
- **Other:** `?:`, `,` (comma), `sizeof`, casts `(type)`, address-of `&`,
  dereference `*`, member `.` and `->`, and subscript `[]`.

## 16-bit model notes

- `>>` is an **arithmetic** (sign-extending) shift on signed operands and a
  **logical** (zero-fill) shift on unsigned operands — use `unsigned` /
  `unsigned long` when you need a guaranteed zero-fill shift.
- Shifts and bitwise operators act at the operand's width: 16-bit for `int`,
  32-bit for `long`. Cast or promote to `long` before shifting if you need more
  than 16 bits:

  ```c
  long page = (long)x << 20;   /* shift in 32-bit, not 16-bit */
  ```

- The optimizer rewrites multiply/divide by a power-of-two constant into the
  equivalent shift.
- `%` requires integer operands. For a floating-point remainder use
  [`fmodf`](standard-lib/08-math.md); `floatx % floaty` is rejected at compile time.

!!! note "Undefined integer division cases"
    C leaves `/` and `%` undefined when the divisor is zero, and also leaves
    `LONG_MIN / -1L` undefined because the mathematical quotient is not
    representable. Portable programs must prevent these cases.

    For deterministic runtime diagnostics, DCC's 16- and 32-bit signed and
    unsigned divide/remainder helpers currently use an all-ones quotient and
    the unchanged dividend as the remainder for a zero divisor. The specialized
    fused `(unsigned16 * unsigned16) % unsigned16` helper has a 16-bit result
    ABI; for a zero modulus its fused dividend sentinel is therefore
    `low16(a * b)`, not either input operand or the full 32-bit product.
    `LONG_MIN / -1L` follows two's-complement wraparound (`LONG_MIN`) and
    `LONG_MIN % -1L` yields zero. These fallbacks are runtime behavior, not a
    portable C guarantee.

## Mixed-type arithmetic conversions

Mixed-type expressions follow the usual arithmetic conversions: **`float`
outranks `long`, which outranks 16-bit `int` / `short`.** So `longv + 1.0f`,
`intv < floatv`, and `cond ? 2 : 3.5f` are all computed in `float`, and the
result stays `float` until you cast it back.

```c
long  n = 100000L;
float f = n + 1.0f;     /* computed in float */
int   k = (int)(cond ? 2 : 3.5f);  /* the ?: is float; cast back to int */
```

Mind the single-precision limit whenever a wide `long` meets a `float`: the
integer side rounds to the nearest `float` first, so compare as integers when
you need full 32-bit precision. See [Floating-point math](standard-lib/08-math.md) for the
details.
