# Fixed-width integers (`stdint.h`)

Include [`stdint.h`](13-stdint.md) for fixed-width integer typedefs and limit
macros that match the DCC C Compiler target model.

## Types and macros

<!-- STDINT-SYMBOL-TABLE: all -->

## Runtime model

The DCC C Compiler is a 16-bit target with 8-bit `char`, 16-bit `int`, and 32-bit `long`.
`stdint.h` names the exact-width, least-width, fast-width, pointer-width, and
maximum-width integer types that fit that runtime model without adding new
runtime support. There are no 64-bit integer typedefs or limit macros.

The `int_fastN_t` / `uint_fastN_t` types are ordinary integer typedefs, not
register-backed types. As in C99, "fast" means the type the target operates on
fastest among those at least `N` bits wide: 8-bit maps to `char` (native on the
Z80), 16-bit to `int` (the natural word), and 32-bit to `long`.

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
