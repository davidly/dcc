# Variadic arguments (`stdarg.h`)

Include [`stdarg.h`](10-stdarg.md) to read arguments passed through an ellipsis
parameter list.

## Types and Macros

<!-- STDARG-SYMBOL-TABLE: all -->

## Runtime model

`va_list` is a `char *` cursor walking the caller's stack frame. Start it with
`va_start`, fetch each argument with `va_arg`, and finish with `va_end`.

The DCC C Compiler uses 16-bit `int` and pointer arguments, 32-bit `long` and `float`
arguments, and no `double`. Pass the exact promoted type to `va_arg`: use `int`
for narrow integer types after default promotions, `long` for long values, and
`float` for floating values on this target.

## Variadic functions

The last named parameter passed to `va_start` must be the parameter immediately
before the ellipsis:

```c
#include <stdarg.h>

int sum(int count, ...)
{
    va_list ap;
    int total = 0;
    int i;

    va_start(ap, count);
    for (i = 0; i < count; ++i)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}
```

A `va_list` can be forwarded straight to `vprintf`, `vfprintf`, or `vsprintf`
to build `printf`-style wrappers without re-parsing the arguments. See
[Console and file I/O](05-stdio.md#printf-family-output) for the supported
formatted-output subset and the compiler options required for long or float
formatting.

```c
#include <stdarg.h>
#include <stdio.h>

void log_msg(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
```
