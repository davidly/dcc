# Diagnostics (`assert.h`)

Include [`assert.h`](01-assert.md) for the runtime `assert` macro and the C11
`static_assert` spelling for compile-time assertions.

## Macros

<!-- ASSERT-SYMBOL-TABLE: all -->

## Compile-time assertions

`_Static_assert(integer_constant_expression, message)` checks an assumption
while the program is compiled. DCC accepts the declaration at file scope,
block scope, and among `struct` or `union` members. The message must be a string
literal. A false expression stops compilation and reports the message.

Including `assert.h` defines `static_assert` as the standard C11 macro spelling
for `_Static_assert`. Compile-time assertions are not affected by `NDEBUG` and
do not add code or data to the generated program.

```c
#include <assert.h>
#include <stddef.h>

static_assert(sizeof(int) == 2, "this program requires 16-bit int");

struct Record {
    char tag;
    int value;
    _Static_assert(sizeof(long) == 4, "long must be 32 bits");
};

void check_layout(void)
{
    static_assert(offsetof(struct Record, value) == 1,
                  "unexpected Record layout");
}
```

Use a static assertion for properties the compiler can determine, such as
target type sizes, array relationships, enumerator values, and aggregate
layout. Its first operand must be an integer constant expression.

## Runtime model

`assert` is implemented in the header, not by a separate debug runtime. When
`NDEBUG` is not defined, `assert(expression)` evaluates `expression`. If it is
false, the header's helper prints a diagnostic to `stderr` with `fprintf`,
flushes `stderr`, and terminates the program with `exit(1)`.

When `NDEBUG` is defined before including `assert.h`, `assert(expression)`
expands to `((void)0)` and does not evaluate `expression`.

## Runtime assertions

Use assertions for programmer assumptions, not normal input validation:

```c
#include <assert.h>

void use_buffer(char *buf, int len)
{
    assert(buf != 0);
    assert(len > 0);
}
```

The diagnostic includes the failed expression, source file, and line number.
Because failed assertions call `exit`, normal exit-time flushing and cleanup
still run.
