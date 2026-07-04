# Fixed-width integers (`stdint.h`)

Include [`stdint.h`](13-stdint.md) for fixed-width integer typedefs that match
The DCC C Compiler target model.

## Types

<!-- STDINT-SYMBOL-TABLE: all -->

## Runtime model

The DCC C Compiler is a 16-bit target with 8-bit `char`, 16-bit `int`, and 32-bit `long`.
`stdint.h` names those exact storage widths without adding new runtime support.

`wchar_t` is also defined here if no earlier header has defined it. The type is
an unsigned 16-bit `int`, matching [Common definitions](12-stddef.md).

## Type selection

Use fixed-width names when the storage size is part of a file format, protocol,
or packed structure:

```c
#include <stdint.h>

struct rec {
    uint8_t  tag;
    uint16_t count;
    uint32_t checksum;
};
```

Use the ordinary C types when you only need the natural target word size.

For formatted output, use the underlying DCC C Compiler type. For example, `uint32_t` is an
`unsigned long`, so print it with `%lu` or `%lx` and compile with `-fl` /
`-flongio`.
