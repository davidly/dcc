#include <stdio.h>
#include <string.h>

/* Regression test for a real dcc bug: a LOCAL (function-scope) function-
 * pointer variable initialized from an external/RTL function - here
 * memcmp, chosen because it predates this bug fix and has nothing to do
 * with stricmp - failed to link ("U ... ld hl,__mcmp") because
 * gen_ident()'s function-name-decays-to-address path never called
 * emit_extrn_if_needed() for the target symbol. A GLOBAL function
 * pointer with the same initializer works fine (dcc_data.c's own
 * initializer codegen already calls it), which is why this went
 * unnoticed: every function-pointer test added so far (tests/tfpcall.c)
 * happened to use a global fp variable. User-defined, same-file
 * functions were also unaffected (no EXTRN needed for a local public
 * label), which is why a plain add(int,int)-style local fp test alone
 * would not have caught this. */

int main(void)
{
    int (*fp)(const void *, const void *, size_t) = memcmp;

    printf("%d\n", fp("abc", "abc", 3));
    printf("%d\n", fp("abc", "abd", 3) < 0);
    printf("%d\n", fp("abd", "abc", 3) > 0);

    return 0;
}
