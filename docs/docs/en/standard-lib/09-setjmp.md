# Non-local jumps (`setjmp.h`)

Include [`setjmp.h`](09-setjmp.md) to save a stack context and return to it
later with a non-local jump.

## Types and Macros

<!-- SETJMP-SYMBOL-TABLE: all -->

## Functions

<!-- SETJMP-FUNCTION-TABLE: all -->

## Runtime model

`jmp_buf` is an 8-byte buffer holding the saved return address, stack pointer,
IX frame pointer, and callee-saved IY register. DCC C Compiler declares
`setjmp` as an ordinary function; the frameless runtime entry captures the
caller context directly so the call behaves like the C89 non-local jump
primitive.

`setjmp(env)` returns `0` when the context is saved directly. A later
`longjmp(env, val)` restores that context and makes the saved `setjmp` return
`val`, or `1` if `val` is `0`.

## Non-local jump pattern

Keep the `jmp_buf` alive until every possible `longjmp` using it is finished.
File-scope or caller-owned storage is safest:

```c
#include <setjmp.h>

jmp_buf env;

void fail(void)
{
    longjmp(env, 7);
}

int main(void)
{
    int code;

    code = setjmp(env);
    if (code == 0) {
        fail();
    } else {
        return code;
    }
    return 0;
}
```

After a `longjmp`, automatic variables in the restored function have the same
practical caveats as C89: values changed after `setjmp` should not be relied on
unless they are `volatile`.
