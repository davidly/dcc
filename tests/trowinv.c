#include <stdio.h>

/* Regression: the loop-invariant 2D-array row hoist must NOT cache a row
 * whose subscript reads memory the loop body itself writes. Here the row
 * index is a[1], and the body stores through a[k]; when k reaches 1 the row
 * source changes, so t[a[1]][k] must be re-evaluated every iteration.
 *
 * Expected transitions (t row0 = {1,1,1,1}, row1 = {0,9,9,9}):
 *   k=0: a[0] = t[a[1]=0][0] = 1
 *   k=1: a[1] = t[a[1]=0][1] = 1   (a[1] becomes 1)
 *   k=2: a[2] = t[a[1]=1][2] = 9
 *   k=3: a[3] = t[a[1]=1][3] = 9
 * -> a = {1,1,9,9}. Caching row a[1]=0 across the loop wrongly yields
 *    {1,1,1,1}. */

static int a[4];
static int t[2][4] = { { 1, 1, 1, 1 }, { 0, 9, 9, 9 } };

int main(void)
{
    int k;

    a[0] = 0; a[1] = 0; a[2] = 0; a[3] = 0;

    for (k = 0; k < 4; k++)
        a[k] = t[a[1]][k];

    printf("trowinv %d %d %d %d\n", a[0], a[1], a[2], a[3]);
    return !(a[0] == 1 && a[1] == 1 && a[2] == 9 && a[3] == 9);
}
