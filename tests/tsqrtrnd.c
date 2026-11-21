/* sqrtf is correctly rounded. Newton iteration in float, with a one-ulp
 * correction, left about a quarter of results one ulp off, sqrtf(2.0f)
 * among them; an integer square root of the significand is exact. */
#include <stdio.h>
#include <math.h>
#include <stdint.h>

static const uint32_t cases[][2] = {
    { 0x40000000UL, 0x3fb504f3UL },
    { 0x00000001UL, 0x1a3504f3UL },
    { 0x7f7fffffUL, 0x5f7fffffUL },
    { 0x3f7fffffUL, 0x3f7fffffUL },
    { 0x3c88596cUL, 0x3e041bd4UL },
    { 0x34733ac5UL, 0x39f98879UL },
    { 0x5e98c13fUL, 0x4f0bd4a8UL },
    { 0x0e625fc9UL, 0x26f0bb55UL },
    { 0x23a5a0e3UL, 0x31919a8dUL },
    { 0x120f3e62UL, 0x28bf7ec0UL },
    { 0x153816b6UL, 0x2a59163dUL },
    { 0x2d61d521UL, 0x36707190UL },
    { 0x3f800000UL, 0x3f800000UL },     /* 1 */
    { 0x41100000UL, 0x40400000UL },     /* 9 */
    { 0x4b7fffffUL, 0x457fffffUL },     /* 2^24 - 1 */
    { 0x00000001UL, 0x1a3504f3UL },     /* smallest subnormal */
    { 0x007fffffUL, 0x1fffffffUL },     /* largest subnormal */
    { 0x7f7fffffUL, 0x5f7fffffUL },     /* FLT_MAX */
};

static float bits_float(uint32_t u)
{
    union { float f; uint32_t u; } x;

    x.u = u;
    return x.f;
}

static uint32_t float_bits(float f)
{
    union { float f; uint32_t u; } x;

    x.f = f;
    return x.u;
}

int main(void)
{
    int i;
    int fails = 0;

    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
        uint32_t got = float_bits(sqrtf(bits_float(cases[i][0])));

        if (got != cases[i][1]) {
            printf("sqrtf(%08lx) = %08lx, expected %08lx\n", (unsigned long)cases[i][0],
                   (unsigned long)got, (unsigned long)cases[i][1]);
            ++fails;
        }
    }
    if (fails)
        printf("tsqrtrnd failed %d\n", fails);
    else
        printf("tsqrtrnd passed\n");
    return 0;
}
