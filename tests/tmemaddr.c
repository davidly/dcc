/* File-scope addresses of members reached through pointer arithmetic.
 * Adapted from SDCC's GCC torture execute test const-addr-expr-1. */
#include <stdio.h>

struct Item {
    char prefix;
    int value;
    long tail;
};

static struct Item items[] = {
    { 1, 101, 1001L },
    { 2, 202, 2002L },
    { 3, 303, 3003L }
};

static int *first_value = (int *)&((items)->value);
static int *second_value = (int *)&((items + 1)->value);
static long *third_tail = (long *)&((items + 2)->tail);

int main(void)
{
    int failures = 0;

    if (*first_value != 101)
        ++failures;
    if (*second_value != 202)
        ++failures;
    if (*third_tail != 3003L)
        ++failures;

    *second_value = 222;
    *third_tail = 3333L;
    if (items[1].value != 222 || items[2].tail != 3333L)
        ++failures;

    if (failures) {
        printf("tmemaddr failed: %d\n", failures);
        return 1;
    }
    printf("tmemaddr completed with great success\n");
    return 0;
}
