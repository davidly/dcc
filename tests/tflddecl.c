/* Per-member declarator modifiers in a struct declaration list.
 * Adapted from SDCC's GCC torture execute test 950809-1. */
#include <stdio.h>

struct Mixed {
    int *sp, scalar, *sc, values[2];
    unsigned char *cp, byte;
    long *lp, wide;
};

static void rotate(struct Mixed *item)
{
    int *source = item->sc;
    int first = source[0];
    int second = source[1];
    int third = source[2];
    int value0 = item->values[0];
    int value1 = item->values[1];

    source[2] = first;
    source[0] = value1;
    item->values[1] = value0;
    item->values[0] = third;
    item->scalar = second;
    item->sp = source;
}

int main(void)
{
    struct Mixed item;
    int source[3];
    unsigned char byte_value;
    long long_value;
    int failures = 0;

    source[0] = 2;
    source[1] = 3;
    source[2] = 4;
    item.sc = source;
    item.values[0] = 10;
    item.values[1] = 11;
    rotate(&item);

    if (item.sp[2] != 2 || item.sp[0] != 11 ||
        item.values[0] != 4 || item.values[1] != 10 || item.scalar != 3)
        ++failures;

    byte_value = 29;
    item.cp = &byte_value;
    item.byte = 17;
    if (*item.cp != 29 || item.byte != 17)
        ++failures;

    long_value = 300000L;
    item.lp = &long_value;
    item.wide = 400000L;
    if (*item.lp != 300000L || item.wide != 400000L)
        ++failures;

    if (failures) {
        printf("tflddecl failed: %d\n", failures);
        return 1;
    }
    printf("tflddecl completed with great success\n");
    return 0;
}
