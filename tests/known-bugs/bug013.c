/* BUG-013: an unsigned bit-field narrower than int must promote to int
 * (C89 3.2.1.1, C99 6.3.1.1), and dcc does so for `bf - 1000` written
 * directly. But once the bit-field is the operand of unary - or ~, of any
 * other binary operator, or an arm of ?:, the result is typed unsigned int:
 * it is zero-extended when widened to long and compared as unsigned. */
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

int main(void)
{
    int16_t i;
    unary();
    printf("unary %ld %ld %ld\n", (long)out[0], (long)out[1], (long)out[2]);
    for (i = 0; i < 9; i++)
        printf("binary %d %ld\n", i, (long)binary(i));
    for (i = 0; i < 3; i++)
        printf("conditional %d %ld\n", i, (long)conditional(i));
    return 0;
}
