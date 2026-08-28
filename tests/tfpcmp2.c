/* A lazy wide parameter must be loaded from its stack home before a float
 * comparison.  Reduced from GCC torture cmpsf-1.c. */
#include <stdio.h>

static int equal(float x, float y) { return x == y; }
static int less(float x, float y) { return x < y; }

int main(void)
{
    float values[] = { 0.0F, 1.0F, -1.0F };

    if (!equal(values[0], values[0]) || equal(values[0], values[1]) ||
        !less(values[0], values[1]) || less(values[1], values[0]) ||
        !less(values[2], values[0])) {
        printf("tfpcmp2 failed\n");
        return 1;
    }
    printf("tfpcmp2 completed with great success\n");
    return 0;
}
