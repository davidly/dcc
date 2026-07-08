/*
 * Compile-fail guard: stack_check pragmas, including malformed variants, must
 * not emit their own diagnostics.  The only expected diagnostic is the #error
 * below, so this catches accidental warning/error behavior for pragma parsing.
 */
#pragma stack_check(on)
#pragma stack_check ( off )
#pragma stack_check(auto)

#if 0
#pragma stack_check(on)
#endif

#error stack_check pragma diagnostics smoke test

int main(void)
{
    return 0;
}