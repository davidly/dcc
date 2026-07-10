#include <stdio.h>
#include <string.h>

/* Regression test for stricmp (ASCII case-insensitive strcmp, a CP/M-
 * appropriate assumption since there is no locale). Covers direct calls
 * (fastcall __icf), a global function pointer (general __sicm entry, the
 * long-established coverage pattern from tests/tfpcall.c), and a LOCAL
 * function pointer - the shape that turned up a real, unrelated
 * pre-existing bug: dcc_ast_gen_expr.c's gen_ident() never called
 * emit_extrn_if_needed() for a function name decaying to its address in a
 * LOCAL (not global) initializer, so any RTL function assigned to a
 * local function-pointer variable failed to link ("U ... ld hl,__mcmp")
 * - invisible before because every previous function-pointer test in
 * this suite happened to use a global fp variable. See tests/tlocalfp.c
 * for a test isolating that bug specifically against memcmp. */

int sgn(int x) { return x < 0 ? -1 : (x > 0 ? 1 : 0); }

int (*g_fp_stricmp)(const char *, const char *) = stricmp;

int main(void)
{
    printf("%d\n", sgn(stricmp("hello", "HELLO")));
    printf("%d\n", sgn(stricmp("abc", "abd")));
    printf("%d\n", sgn(stricmp("ABD", "abc")));
    printf("%d\n", sgn(stricmp("", "")));
    printf("%d\n", sgn(stricmp("a", "")));
    printf("%d\n", sgn(stricmp("MiXeD", "mixed")));
    printf("%d\n", sgn(stricmp("Zebra", "apple")));

    printf("%d\n", sgn(g_fp_stricmp("Global", "GLOBAL")));

    int (*local_fp)(const char *, const char *) = stricmp;
    printf("%d\n", sgn(local_fp("Local", "LOCAL")));

    return 0;
}
