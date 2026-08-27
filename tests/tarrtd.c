/*
 * tarrtd.c - a typedef'd array type used as the element type of another
 * array declaration must fold into a single multi-dimensional array shape,
 * exactly like a spelled-out multi-dimensional array:
 *
 *     typedef unsigned long ARR2[2];
 *     ARR2 table[2];              == unsigned long table[2][2]
 *
 * dcc used to drop the typedef's own dimension whenever the declarator also
 * added a `[N]` of its own, leaving the symbol looking like a 1-D array to
 * the 2-D-index recognizer and rejecting `table[i][j] = x;` outright with
 * DCC-E1002 ("unsupported expression statement").  Covers the reported global
 * case, the same shape as a local (a separate codegen path), and a global
 * with a braced initializer (exercises the dim-count-driven initializer
 * walk, not just plain indexing).
 */
#include <stdio.h>

static int fails;

static void chk(const char *name, long got, long exp)
{
    if (got != exp) {
        printf("FAIL %s got %ld expected %ld\n", name, got, exp);
        fails++;
    }
}

typedef unsigned long ARR2[2];

ARR2 table[2];
ARR2 itab[2] = { { 1, 2 }, { 3, 4 } };

int main(void)
{
    ARR2 ltab[2];

    fails = 0;

    /* global typedef'd 2-D array: write then read all four elements */
    table[0][0] = 111;
    table[0][1] = 222;
    table[1][0] = 333;
    table[1][1] = 444;
    chk("global_00", table[0][0], 111);
    chk("global_01", table[0][1], 222);
    chk("global_10", table[1][0], 333);
    chk("global_11", table[1][1], 444);

    /* local typedef'd 2-D array: separate frame-sizing/codegen path */
    ltab[0][0] = 11;
    ltab[0][1] = 22;
    ltab[1][0] = 33;
    ltab[1][1] = 44;
    chk("local_00", ltab[0][0], 11);
    chk("local_01", ltab[0][1], 22);
    chk("local_10", ltab[1][0], 33);
    chk("local_11", ltab[1][1], 44);

    /* global typedef'd 2-D array with a braced initializer */
    chk("init_00", itab[0][0], 1);
    chk("init_01", itab[0][1], 2);
    chk("init_10", itab[1][0], 3);
    chk("init_11", itab[1][1], 4);
    itab[1][1] = 999;
    chk("init_11_store", itab[1][1], 999);

    if (fails) {
        printf("tarrtd failed: %d\n", fails);
        return 1;
    }
    printf("tarrtd completed with great success\n");
    return 0;
}
