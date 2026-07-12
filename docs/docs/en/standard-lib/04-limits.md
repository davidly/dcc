# Integer limits (`limits.h`)

Include [`limits.h`](04-limits.md) for the integer type ranges used by the DCC C Compiler
target model.

## Macros

<!-- LIMITS-SYMBOL-TABLE: all -->

## Runtime model

The DCC C Compiler uses 8-bit `char`, 16-bit `short` and `int`, and 32-bit `long`. Plain `char`
is signed, so `CHAR_MIN` and `CHAR_MAX` match `SCHAR_MIN` and `SCHAR_MAX`.

The header also provides `UINT32_MAX` for compatibility with code that uses the
32-bit unsigned limit name alongside `stdint.h`.

The runtime uses a fixed single-byte C/ASCII locale (`MB_CUR_MAX` is 1 in
`stdlib.h`). `limits.h` does not currently define `MB_LEN_MAX`.

## Range choices

Use `long` or `unsigned long` when values can exceed the 16-bit `int` range:

```c
#include <limits.h>
#include <stdio.h>

long total = 0;
if (total > INT_MAX)
    puts("needs long formatting");
```

For formatted output of 32-bit values, use the `l` length modifier. Literal
formats are detected automatically; `-fl` / `-flongio` only forces long-format
support on every call. See
[Console and file I/O](05-stdio.md#printf-family-output).
