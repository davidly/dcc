/*
 * tinlnpar.c - regression test for a dcc compiler crash (segfault) found
 * while working on tests/forint.c: an inline candidate's "guard" statement
 * (dcc_func.c's inline_return_expr_from_seq, the `if (cond) die("msg");`
 * side-effect-only pattern documented there) is also allowed to reassign
 * one of the function's own parameters, e.g. `if (cond) idx = 0;` ahead of
 * a `return` that uses idx. Substituting a call site's argument EXPRESSION
 * into what's now an assignment TARGET isn't a valid lvalue ("3 = 0" for a
 * call like f(3)) - gen_assign_ast then called find_sym on an AST_IDENT
 * node the substitution never populated a name for, dereferencing a NULL
 * pointer inside strcmp and crashing the compiler outright, not just
 * miscompiling.
 *
 * Fixed in inline_expr_is_simple's AST_ASSIGN case (dcc_func.c): declines
 * inlining only when the assignment TARGET is directly a bare parameter
 * identifier - deliberately narrower than inline_expr_touches_param (used
 * by the pre-existing, analogous TOK_INC/TOK_DEC check just above it),
 * which checks for a parameter ANYWHERE in the target expression and so
 * would also wrongly decline `arr[idx] = v;` (an array-element target that
 * merely READS a parameter as part of its index - perfectly sound to
 * inline) - an earlier, broader version of this fix did exactly that and
 * regressed several already-working inline candidates elsewhere in the
 * suite before being narrowed to match this test.
 */
#include <stdio.h>

/* The original minimal crash repro: a guard reassigning a parameter,
 * directly ahead of a return using it. Must not crash the compiler, and
 * (whether or not dcc's inliner ends up substituting this call - either
 * outcome is fine) must produce the correct answer either way. */
static inline int clamp_low(int cond, int idx)
{
    if (cond) idx = 0;
    return idx + 1;
}

/* The other half of the fix: an array-element assignment target that only
 * READS parameters (never reassigns one) must still be considered eligible
 * for inlining, not declined just because a parameter appears somewhere
 * within the target expression. */
static unsigned char mem[16];
static inline void mem_store(int base, int idx, int v)
{
    mem[base + idx] = (unsigned char)v;
}

int main(void)
{
    int a, b, c;

    a = clamp_low(1, 5);   /* cond true: idx forced to 0, result 1 */
    b = clamp_low(0, 5);   /* cond false: idx stays 5, result 6 */
    mem_store(2, 3, 42);
    c = mem[5];

    printf("a=%d b=%d c=%d\n", a, b, c);
    return 0;
}
