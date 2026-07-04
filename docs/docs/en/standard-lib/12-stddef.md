# Common definitions (`stddef.h`)

Include [`stddef.h`](12-stddef.md) for common C definitions used by library
headers and portable data-structure code.

## Types and Macros

<!-- STDDEF-SYMBOL-TABLE: all -->

## Runtime model

The DCC C Compiler is a 16-bit target: `size_t`, `ptrdiff_t`, and object pointers are all
16-bit. `wchar_t` is an unsigned 16-bit type and matches the representation used
for wide string literals.

`NULL` is defined as `0` if no earlier header has defined it. It is suitable as
a null pointer constant in pointer contexts.

## Offsets

`offsetof(type, member)` is folded by the compiler and produces the byte offset
of a struct or union member. It accepts nested member designators and constant
array indexes:

```c
#include <stddef.h>

struct rec {
    char tag;
    int  value;
};

int off = offsetof(struct rec, value);   /* 1 on this 16-bit target */
```

Use `ptrdiff_t` for pointer subtraction results and `size_t` for sizes and
object counts that follow the standard library interfaces.

```c
#include <stddef.h>

ptrdiff_t span(char *first, char *last)
{
    return last - first;
}
```
