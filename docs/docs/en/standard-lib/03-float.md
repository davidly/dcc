# Floating-point limits (`float.h`)

Include [`float.h`](03-float.md) for the floating-point range and precision
macros.

## Macros

<!-- FLOAT-SYMBOL-TABLE: all -->

## Runtime model

The DCC C Compiler has only one floating type: 32-bit IEEE-style `float`. There is no distinct
`double` or `long double` representation. The `DBL_*` and `LDBL_*` macros are
therefore aliases of the `FLT_*` values so portable source can compile, but they
intentionally describe the same single-precision target.

## Precision

`FLT_MANT_DIG` is 24, so integers up to 2^24 are exactly representable and
larger integers may round when converted to `float`. `FLT_DIG` is 6, reflecting
the number of decimal digits that can be represented without change on a round
trip through this target's `float` format.

Use `FLT_EPSILON` for small tolerance checks rather than expecting decimal
results to compare exactly:

```c
#include <float.h>

int nearly_same(float a, float b)
{
    float d = a - b;
    if (d < 0.0f)
        d = -d;
    return d <= FLT_EPSILON;
}
```

See [Floating-point math](08-math.md) for math-library accuracy and mixed-type
gotchas.
