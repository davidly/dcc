/*
 * A wide value defined in a loop body and returned after the loop is live
 * across the backedge.  Backend slot allocation must not let loop-header
 * temporaries overwrite that carried value.  Found via GCC torture
 * pr78675.c.
 */
#include <stdio.h>

static long counter;

static long carried(long x)
{
    long result;

    while (counter < 1) {
        result = counter && x;
        ++counter;
    }
    return result;
}

static int check(long start, long x)
{
    long got;

    counter = start;
    got = carried(x);
    if (got != 0) {
        printf("tloopcar failed: start=%ld x=%ld got=%ld\n",
               start, x, got);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (check(0, 0) || check(0, 1) || check(0, 25) ||
        check(-64, 0) || check(-64, 1) || check(-64, 25))
        return 1;

    printf("tloopcar completed with great success\n");
    return 0;
}
