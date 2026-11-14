/* BUG-057: a fused 32-bit local increment used as an addition operand
 * reads an unwritten backend slot instead of the updated named home.
 * A later loop retains the spilled/PHI schedule even when it never runs.
 * Decrement, literal RHS and no-loop neighbors are passing controls. */
#include <stdint.h>
#include <stdio.h>
static int8_t g = -2;

static int prefix_add(void)
{
    int32_t x = 0;
    uint16_t limit = 0;
    if ((int32_t)1) { x = 1; }
    g = (++x) + (g ^ (int32_t)63);
    for (int8_t i = 2; (uint16_t)i < limit; ++i)
        --x;
    return 1;
}

static int compound_add(void)
{
    int32_t x = 0;
    uint16_t limit = 0;
    if ((int32_t)1) { x = 1; }
    g = (x += 1) + (g ^ (int32_t)63);
    for (int8_t i = 2; (uint16_t)i < limit; ++i)
        --x;
    return 1;
}

static int prefix_sub(void)
{
    int32_t x = 0;
    uint16_t limit = 0;
    if ((int32_t)1) { x = 1; }
    g = (--x) + (g ^ (int32_t)63);
    for (int8_t i = 2; (uint16_t)i < limit; ++i)
        --x;
    return 1;
}

static int right_add(void)
{
    int32_t x = 0;
    uint16_t limit = 0;
    if ((int32_t)1) { x = 1; }
    g = (g ^ (int32_t)63) + (++x);
    for (int8_t i = 2; (uint16_t)i < limit; ++i)
        --x;
    return 1;
}

static int constant_rhs(void)
{
    int32_t x = 0;
    uint16_t limit = 0;
    if ((int32_t)1) { x = 1; }
    g = (++x) + (int32_t)7;
    for (int8_t i = 2; (uint16_t)i < limit; ++i)
        --x;
    return 1;
}

static int no_loop(void)
{
    int32_t x = 0;
    uint16_t limit = 0;
    if ((int32_t)1) { x = 1; }
    g = (++x) + (g ^ (int32_t)63);
    return 1;
}

int main(void)
{
    g = -2;
    prefix_add();
    printf("prefix_add %d\n", (int)g);
    g = -2;
    compound_add();
    printf("compound_add %d\n", (int)g);
    g = -2;
    prefix_sub();
    printf("prefix_sub %d\n", (int)g);
    g = -2;
    right_add();
    printf("right_add %d\n", (int)g);
    g = -2;
    constant_rhs();
    printf("constant_rhs %d\n", (int)g);
    g = -2;
    no_loop();
    printf("no_loop %d\n", (int)g);
    return 0;
}
