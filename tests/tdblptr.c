/*
 * tdblptr.c - dereferencing a pointer-to-pointer-to-char twice (**pp) and
 * using the result directly as an int-context rvalue must read exactly one
 * byte from the final pointee, not two. mir_lower_lvalue_address's special
 * case for a double '*' chain (dcc_mir.c) mislabeled the intermediate
 * address value's type as one pointer level too deep, which fooled a later
 * generic MIR_LOAD_INDIRECT type/size repair pass into widening the correct
 * 1-byte char load back up to a 2-byte pointer-style load - reading the
 * next array element's byte into the high byte of the result. Splitting the
 * double dereference across an intermediate char-typed assignment happened
 * to sidestep the bug, which is what let it hide.
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

static char varr[] = { 1, 2, 3, 4 };
static unsigned char uarr[] = { 0xfe, 0xfd };

int main(void)
{
    char *vp;
    char **vpp;
    char c;
    int i;
    unsigned char *up;
    unsigned char **upp;

    fails = 0;

    vp = varr;
    vpp = &vp;
    chk("direct_int_ctx", (int)**vpp, 1);
    chk("printf_no_cast", **vpp, 1);
    i = **vpp;
    chk("assign_to_int", i, 1);
    c = **vpp;
    chk("assign_to_char", c, 1);
    vp++;
    chk("advanced_direct", (int)**vpp, 2);

    /* an unsigned final pointee must zero-extend, not sign-extend, once the
     * width is corrected back down to one byte */
    up = uarr;
    upp = &up;
    chk("unsigned_direct", (int)**upp, 0xfe);
    i = **upp;
    chk("unsigned_assign", i, 0xfe);

    if (fails) {
        printf("tdblptr failed: %d\n", fails);
        return 1;
    }
    printf("tdblptr completed with great success\n");
    return 0;
}
