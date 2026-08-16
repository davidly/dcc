/**
 * @file bsearch.c
 * @brief Demonstrates dcc's runtime bsearch with present and absent keys.
 *
 * @par Role
 * Provides a deterministic, no-input CP/M sample that searches a fixed sorted
 * integer array and prints the index of each hit or a not-found result.
 *
 * @par Key entry points
 * main() drives the sample; cmp_int() supplies the ordering callback.
 *
 * @par Boundary
 * DCCRTL.MAC owns the bsearch implementation. tests/tbsearch.c provides the
 * comprehensive validation; this file is an API example, not a replacement.
 */

#include <stdio.h>
#include <stdlib.h>

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;

    if (x < y)
        return -1;
    if (x > y)
        return 1;
    return 0;
}

int main(void)
{
    /* bsearch requires the array to be sorted by the same comparator */
    int v[8];
    int keys[5];
    int i;

    v[0] = 2;  v[1] = 5;  v[2] = 8;  v[3] = 13;
    v[4] = 21; v[5] = 34; v[6] = 55; v[7] = 89;

    keys[0] = 2;    /* present (first)  */
    keys[1] = 13;   /* present (middle) */
    keys[2] = 89;   /* present (last)   */
    keys[3] = 7;    /* absent (gap)     */
    keys[4] = 100;  /* absent (above)   */

    for (i = 0; i < 5; i++) {
        const int *hit;

        hit = (const int *)bsearch(&keys[i], v, 8U, sizeof(int), cmp_int);
        if (hit != 0)
            printf("%3d found at index %d\n", keys[i], (int)(hit - v));
        else
            printf("%3d not found\n", keys[i]);
    }

    return 0;
} /* main */



