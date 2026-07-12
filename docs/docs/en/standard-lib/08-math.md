# Floating-point math (`math.h`)

Include [`math.h`](08-math.md). The DCC C Compiler has only 32-bit `float` (no `double`), so
the math entry points are the single-precision `...f` variants. The unsuffixed
C89 names are provided as macro aliases for convenience.

## Standard C names

The standard C89 math names are provided as function-like macros that call the
corresponding single-precision `...f` runtime function.

<!-- MATH-SYMBOL-TABLE: all sorted -->

## Functions

<!-- MATH-FUNCTION-TABLE: all -->

## Runtime model

DCC C Compiler treats `float` as the only floating type. C89 normally declares the
unsuffixed math names (`sqrt`, `sin`, `pow`, and so on) as `double` functions,
but this runtime has no `double`, so [`math.h`](08-math.md) maps those names to
the single-precision `...f` functions with macros.

!!! warning "Float is the biggest size lever"
    A single `float` operator links the shared normalise/round core, and the
    transcendental functions (`expf`/`logf`/`powf` and the trig/hyperbolic
    families) are the heaviest individual features in the runtime. They are
    software routines on the Z80, not hardware floating-point operations, so
    budget both code size and execution time. See the
    [appendix](../appendix/01-dccrtlstrip.md).

## Function groups

The functions follow the conventional C math families, with single-precision
types throughout:

- **Rounding, remainder, and roots:** `fabsf`, `floorf`, `ceilf`, `fmodf`,
  `sqrtf`, `nextafterf`.
- **Exponential and logarithmic:** `expf`, `logf`, `log10f`, `powf`.
- **Trigonometric:** `sinf`, `cosf`, `tanf`, `asinf`, `acosf`, `atanf`,
  `atan2f`.
- **Hyperbolic:** `sinhf`, `coshf`, `tanhf`.
- **Decomposition:** `frexpf`, `ldexpf`, `modff`.

For portable C89 source, the unsuffixed aliases let familiar calls use the same
single-precision runtime functions. For example, `sqrt(x)` expands to
`sqrtf(x)`, and `atan2(y, x)` expands to `atan2f((y), (x))`.

```c
#include <math.h>

float distance(float dx, float dy)
{
  return sqrtf(dx * dx + dy * dy);
}

float heading(float y, float x)
{
  return atan2f(y, x);
}
```

## Printing floats

Use `%f` to print a float. dcc detects literal formats automatically:

```c
float r = sqrtf(2.0f);
printf("%f\n", r);
```

The `-ffloatio` option forces `%f` support on every `printf`-family call; it is
not required for a literal format and does not add floating-point `scanf` input.
Float output is supported by `printf`, `sprintf`, `fprintf`, their `v...` forms,
`snprintf`, and `vsnprintf`. See
[Console and file I/O](05-stdio.md#printf-family-output) for the full formatted
I/O subset.

## Accuracy

The transcendental routines (`expf`/`logf`/`powf`, the trig and hyperbolic
families) are single-precision polynomial and series approximations: expect
roughly 5-6 significant digits on ordinary ranges, not full `float` round-trip
accuracy. The range-reduction in `sinf`/`cosf`/`tanf` uses `fmodf`, so accuracy
gradually degrades for very large arguments.

## Precision and mixed-type gotchas

- **`float` carries about 7 decimal digits (a 24-bit significand).** Integers up
  to 2^24 (16,777,216) are exact; beyond that only some integers are
  representable. `(long)(float)16777217L` is `16777216`, not `16777217`. The
  same rounding applies in constant expressions, `case` labels, and array
  bounds.
- **Comparing a wide integer against a `float` happens in `float`.** The integer
  side converts first, so `16777216L < (float)16777217L` is **false** (the cast
  rounded down to `16777216.0f`). Compare as integers when you need full 32-bit
  precision.
- **Any `float` arm makes a `?:` a `float` expression.** `cond ? 2 : 3.5f`
  yields a `float`, not an `int`; cast the result if you need an integer.
- **A `float` is "true" when its magnitude is nonzero.** `if (f)`, `f ? a : b`,
  `!f`, `f && g`, and `while (f)` test against zero, and both `+0.0f` and
  `-0.0f` count as false.
