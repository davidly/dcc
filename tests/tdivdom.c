/* tdivdom.c - a remainder on a branch and a quotient after it
 *
 * `(c ? x % y : x) < x / y` paired the two divisions into one helper call
 * placed on the branch, so when the branch was skipped the quotient was
 * read from a slot never written that call, holding whatever an earlier
 * call left there. A pair may be computed together only where the first
 * one dominates the second.
 */

#include <stdio.h>

static int fails;
static short g;

static void check(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %lx want %lx\n", name, got, want);
        ++fails;
    }
}

static short take(short v) { return v; }

/* The pointer parameters give the function a frame, as in the fuzzer case
 * that found this. */
short below(unsigned short a, short b, int y, int i, unsigned short *p, short *q, int *r)
{
    unsigned short x;
    x = a;
    {
        return take(((!g) ? (x % y) : x) < ((x / y) - (y >= x)));
    }
}

unsigned quotient_after(unsigned x, unsigned y, int c, int i, unsigned *p, unsigned *q, int *r)
{
    unsigned m = 0;
    if (c)
        m = x % y;
    return m + x / y;
}

int main(void)
{
    g = 0;
    check("below_taken", below(65513u, 0, -1, 0, 0, 0, 0), 1L);
    g = 1;
    check("below_skipped", below(127u, 1, 256, 1, 0, 0, 0), 1L);
    check("quotient_taken", quotient_after(1000u, 7u, 1, 0, 0, 0, 0), 142L + 6L);
    check("quotient_skipped", quotient_after(10u, 3u, 0, 0, 0, 0, 0), 3L);
    if (fails == 0)
        printf("tdivdom passed\n");
    return fails;
}
