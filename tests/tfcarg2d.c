#include <stdio.h>
#include <string.h>

/* Regression test for a real miscompile in every fastcall special case in
 * dcc_ast_gen_expr.c's gen_call_ast (strlen/strchr/memcmp/memset/bdos and
 * the memcpy/memchr/strcpy/strrchr/strstr fastcalls added alongside this
 * test): each one evaluated its arguments with plain ast_gen_expr instead
 * of first checking ast_pointer_expr_type/gen_pointer_expr_ast the way the
 * general (non-fastcall) argument-evaluation loop does. A 2D-array row
 * expression like names[nn++] decays to a pointer (the row's address) in
 * a real function call, but ast_gen_expr alone generated a VALUE-context
 * dereference instead - found via tests/pint.c and tests/adaint.c, both
 * of which build an identifier table with exactly this
 * strcpy(names[nn++], text) shape, and both crashed ("not-implemented z80
 * instruction 0xdd") building their own symbol tables before this fix. */

char names[10][16];
int nn;

int main(void)
{
    strcpy(names[nn++], "identifier");
    strcpy(names[nn++], "second");
    strcpy(names[nn++], "identifier");

    printf("nn=%d [%s][%s][%s]\n", nn, names[0], names[1], names[2]);

    return 0;
}
