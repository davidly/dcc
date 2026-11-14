/* Narrow unsigned bit-field promotion through unary/binary/conditional
 * expressions, widening and comparisons, including scoped type repair. */
#include <stdint.h>
#include <stdio.h>

struct S { uint16_t m0 : 9; uint16_t m1 : 14; uint16_t m2 : 11; };
static struct S s = { 444, 9817, 346 };
static int16_t sel = 1;
static int32_t out[3];

static void unary(void)
{
    out[0] = -s.m0;
    out[1] = ~s.m0;
    out[2] = (1 <= (~s.m1));
}

static int32_t binary(int16_t which)
{
    switch (which) {
    case 0: return (s.m2 | 1) - 1000;
    case 1: return (s.m2 ^ 1) - 1000;
    case 2: return (s.m2 & 3) - 30;
    case 3: return (s.m2 + 1) - 1000;
    case 4: return (s.m2 * 2) - 1000;
    case 5: return (s.m2 / 2) - 1000;
    case 6: return (s.m2 % 7) - 1000;
    case 7: return (s.m2 >> 1) - 1000;
    default: return (s.m2 << 1) - 1000;
    }
}

static int32_t conditional(int16_t which)
{
    if (which == 0)
        return 1 - (sel ? s.m2 : 1);
    if (which == 1)
        return (sel ? s.m2 : 1) - 1000;
    if ((sel ? (-21931) : s.m1) > 1)
        return 5;
    return 9;
}

static void scoped(void)
{
    {
        uint8_t b = (uint8_t)sel;
        out[0] = -s.m0;
        out[1] = (s.m2 + 1) - 1000;
        out[2] = (-3 > sel * (b + s.m0));

    }
}

static int32_t scoped_conditional(void)
{
    {
        uint8_t b = (uint8_t)sel;
        return 1 - (b ? s.m2 : b);
    }
}

int main(void)
{
    int16_t i;
    unary();
    printf("unary %ld %ld %ld\n", (long)out[0], (long)out[1], (long)out[2]);
    for (i = 0; i < 9; i++)
        printf("binary %d %ld\n", i, (long)binary(i));
    for (i = 0; i < 3; i++)
        printf("conditional %d %ld\n", i, (long)conditional(i));
    scoped();
    printf("scoped %ld %ld %ld\n", (long)out[0], (long)out[1], (long)out[2]);
    printf("scoped conditional %ld\n", (long)scoped_conditional());
    return 0;
}
