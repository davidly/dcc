/*
 * tpostai.c - regression coverage for using the value of a post-incremented
 * pointer-array element.
 *
 * Using the VALUE of a post-increment on a pointer-array element used to
 * fail to compile with "error DCC-E1002: unsupported expression statement" -
 * `(arr[i])++;` alone (for its side effect only, value discarded)
 * compiles fine; assigning that expression's value to something is what
 * trips it. No struct is needed to reproduce it. Found via SDCC's
 * regression test gcc-torture-execute-950426-1.c.
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

static char buf[4] = { 'a', 'b', 'c', 'd' };
static char *arr[1];

int main(void)
{
    char *q;

    fails = 0;
    arr[0] = buf;

    q = (arr[0])++;

    chk("postinc_value", *q, 'a');
    chk("postinc_side_effect", *arr[0], 'b');

    if (fails) {
        printf("tpostai failed: %d\n", fails);
        return 1;
    }
    printf("tpostai completed with great success\n");
    return 0;
}
