/* tuaccmp.c - usual arithmetic conversions in integer comparisons */

#include <stdio.h>

static int failures;

static void check(const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s got %d expected %d\n", name, got, expected);
        failures++;
    }
}

static int casted_range_test(int x, int y)
{
    return (x <= y) && ((unsigned int)x >= (unsigned int)y);
}

int main(void)
{
    unsigned int all_bits = ~0U;
    int minus_one = -1;

    printf("tuaccmp start\n");

    /* Same-width casts produce no machine operation.  Comparisons must use
       the post-conversion type, rather than the defining value's type. */
    check("unsigned cast to int", (int)all_bits >= 0, 0);
    check("mixed int unsigned", minus_one >= (unsigned int)0, 1);
    check("two explicit casts", casted_range_test(-1, 0), 1);

    if (failures) {
        printf("tuaccmp failed: %d\n", failures);
        return 1;
    }
    printf("tuaccmp completed with great success\n");
    return 0;
}
