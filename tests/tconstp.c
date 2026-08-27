/*
 * tconstp.c - const-qualified pointer arithmetic and multi-level
 * indirection through const. tc89qual.c already checks that const/volatile
 * qualifiers parse and read correctly, but never advances a pointer-to-const
 * or walks through a pointer-to-pointer-to-const - exactly where a qualifier
 * could accidentally suppress the underlying pointer arithmetic. Modeled on
 * sdcc's regression test const.c (their bug #621531 regression).
 */
#include <stdio.h>

static int fails;

static void chk(const char *name, int got, int exp)
{
    if (got != exp) {
        printf("FAIL %s got %d expected %d\n", name, got, exp);
        fails++;
    }
}

static const char carr[] = { 1, 2, 3, 4 };

int main(void)
{
    const char *cp;
    const char **cpp;

    fails = 0;

    /* the pointer itself isn't const, so it can be advanced even though
     * what it points to can't be written through it */
    cp = carr;
    chk("index0", cp[0], 1);
    cp++;
    chk("preinc_index", cp[0], 2);
    cp = carr + 2;
    chk("ptr_add", *cp, 3);

    /* pointer to pointer to const: advancing the inner pointer through
     * one level of indirection must still land on the right element */
    cp = carr;
    cpp = &cp;
    chk("double_indirect0", **cpp, 1);
    (*cpp)++;
    chk("double_indirect1", **cpp, 2);
    (*cpp)++;
    chk("double_indirect2", **cpp, 3);

    if (fails) {
        printf("tconstp failed: %d\n", fails);
        return 1;
    }
    printf("tconstp completed with great success\n");
    return 0;
}
