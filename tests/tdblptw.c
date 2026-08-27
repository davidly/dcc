/*
 * tdblptw.c - the double-pointer-dereference width bug fixed alongside
 * tdblptr.c is not char-specific: mir_lower_lvalue_address's mislabeled
 * intermediate address type (dcc_mir.c) made the later generic
 * MIR_LOAD_INDIRECT repair pass re-derive the final load's size from that
 * wrong label for ANY pointee width, not just narrow ones. A char pointee
 * got widened from 1 byte to 2 (reading garbage from the next element); a
 * long pointee - wider than a pointer - got the opposite failure, truncated
 * from 4 bytes down to 2 (silently dropping the high half). int is exactly
 * pointer-width, so it happened to load the right number of bytes even
 * under the bug and is included here only as a control.
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

static long larr[] = { 100000L, 200000L, -1L };
static int iarr[] = { 1000, 2000 };

int main(void)
{
    long *lp;
    long **lpp;
    long lv;
    int *ip;
    int **ipp;

    fails = 0;

    lp = larr;
    lpp = &lp;
    chk("long_direct", **lpp, 100000L);
    lv = **lpp;
    chk("long_assign", lv, 100000L);
    lp++;
    chk("long_advanced", **lpp, 200000L);
    lp++;
    chk("long_negative", **lpp, -1L);

    ip = iarr;
    ipp = &ip;
    chk("int_direct", **ipp, 1000);
    ip++;
    chk("int_advanced", **ipp, 2000);

    if (fails) {
        printf("tdblptw failed: %d\n", fails);
        return 1;
    }
    printf("tdblptw completed with great success\n");
    return 0;
}
