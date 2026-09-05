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
    if (setjmp(env) == 0) {
        fail();
    } else {
        return 7;
    }
    return 0;
}
```

The function that called `setjmp` must still be active when `longjmp` runs;
keeping `env` global does not extend that function's lifetime. For portable C,
use `setjmp` in a permitted context such as the controlling comparison above,
not an assignment initializer.

After a `longjmp`, do not rely on changed non-volatile automatic variables in
the restored function. DCC does not promise full standard `volatile`
semantics; use static or caller-owned state when recovery depends on a value.
A non-local jump performs no resource cleanup: release allocations and close
files explicitly on the recovery path.
