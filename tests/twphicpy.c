/* Wide loop-counter copies preserve the pre-update value on PHI edges.
 * Cover for/while, postincrement, constant/variable strides and word carry;
 * narrow counters, do/while and straight-line copies remain controls. */
#include <stdint.h>
#include <stdio.h>

static int32_t for_loop(void)
{
    int32_t l = 0, i;
    for (i = 2; i < 3; i += 1)
        l = i;
    return l;
}

static int32_t for_loop_postinc(void)
{
    int32_t l = 0, i;
    for (i = 10; i < 13; i++)
        l = i;
    return l;
}

static int32_t while_loop(void)
{
    int32_t l = 0, i = 2;
    while (i < 3) {
        l = i;
        i += 1;
    }
    return l;
}

static int32_t narrow_counter(void)
{
    int16_t l = 0, i;
    for (i = 2; i < 3; i += 1)
        l = i;
    return l;
}

static int32_t do_loop(void)
{
    int32_t l = 0, i = 2;
    do {
        l = i;
        i += 1;
    } while (i < 3);
    return l;
}

static int32_t straight_line(void)
{
    int32_t l = 0, i = 2;
    l = i;
    i += 1;
    return l;
}

static int32_t variable_stride(int32_t step)
{
    int32_t l = 0, i;
    for (i = -3; i < 3; i += step)
        l = i;
    return l;
}

static int32_t constant_stride(void)
{
    int32_t l = 0, i;
    for (i = -3; i < 3; i += 2)
        l = i;
    return l;
}

static uint32_t carry_counter(void)
{
    uint32_t l = 0, i;
    for (i = 65535UL; i < 65538UL; i++)
        l = i;
    return l;
}

static int32_t no_iterations(int32_t limit)
{
    int32_t l = 9, i;
    for (i = 2; i < limit; i++)
        l = i;
    return l;
}

int main(void)
{
    printf("for %ld\n", (long)for_loop());
    printf("for postinc %ld\n", (long)for_loop_postinc());
    printf("while %ld\n", (long)while_loop());
    printf("narrow %ld\n", (long)narrow_counter());
    printf("do %ld\n", (long)do_loop());
    printf("straight %ld\n", (long)straight_line());
    printf("stride constant %ld\n", (long)constant_stride());
    printf("stride variable %ld\n", (long)variable_stride(2));
    printf("stride negative %ld\n", (long)variable_stride(1));
    printf("carry %lu\n", (unsigned long)carry_counter());
    printf("zero iterations %ld\n", (long)no_iterations(2));
    return 0;
}
