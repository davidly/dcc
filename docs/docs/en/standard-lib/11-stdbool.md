# Boolean type (`stdbool.h`)

Include [`stdbool.h`](11-stdbool.md) for the C99 boolean spelling used by many
portable C programs.

## Types and Macros

<!-- STDBOOL-SYMBOL-TABLE: all -->

## Runtime model

DCC C Compiler recognizes the native C99 `_Bool` keyword as a first-class 1-byte scalar
type. This header only provides the portable names: `bool` is `_Bool`, `true` is
`1`, and `false` is `0`.

Assignments, casts, initializers, parameter loads, and return values normalize
nonzero `_Bool` / `bool` values to `1`.

## Boolean values

Relational and logical operators already produce `0` or `1`, so they are the
cleanest way to assign boolean state:

```c
#include <stdbool.h>

bool ready = false;

ready = (count > 0);
if (ready)
    puts("go");
```

`__bool_true_false_are_defined` is provided for source compatibility with code
that checks whether `stdbool.h` has supplied the standard names.
