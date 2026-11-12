/* tdblnotw.c - !! of a value just widened to long
 *
 * The spilled emitter fused `!!x` into a truth test of x read from x's frame
 * slot. A long just converted into HL:DE is forwarded to the inner ! without
 * being stored, so the slot was read unwritten and the result was whatever
 * the stack held.
 */

#include <stdio.h>

static unsigned short r[3];
static int fails;

static void check(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %lx want %lx\n", name, got, want);
        ++fails;
    }
}

/* Leave nonzero values where the callees' frames will be. */
static int dirty(int a, int b, int c)
{
    volatile int z[6];

    z[0] = a; z[1] = b; z[2] = c; z[3] = a; z[4] = b; z[5] = -1;
    return z[5];
}

static int if_dblnot(int a, int i) { { int k = a; if (!!((long)r[i])) return 1 + k - k; return 0; } }
static int ret_dblnot(int a, int i) { { int k = a; return !!((long)r[i]) + k - k; } }
static int ret_dblnot_plain(int i) { return !!((long)r[i]); }

int main(void)
{
    r[0] = 0;
    r[1] = 7;
    dirty(-1, -1, -1);
    check("if_dblnot_0", if_dblnot(9473, 0), 0L);
    dirty(-1, -1, -1);
    check("if_dblnot_7", if_dblnot(9473, 1), 1L);
    dirty(-1, -1, -1);
    check("ret_dblnot_0", ret_dblnot(9473, 0), 0L);
    dirty(-1, -1, -1);
    check("ret_dblnot_plain_0", ret_dblnot_plain(0), 0L);
    if (fails == 0)
        printf("tdblnotw passed\n");
    return fails;
}
