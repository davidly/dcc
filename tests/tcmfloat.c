/* Integer compound assignments with float operands across integer widths,
 * signedness, local/global storage, variable/constant operands and operators. */
#include <stdint.h>
#include <stdio.h>

static float f;
static int n;
static int32_t l;
static uint32_t u;
static uint16_t us;
static int8_t c8;

int main(void)
{
    int16_t s = 7;
    f = 2.5f;
    n = 3; l = 5; u = 7; us = 9; c8 = 2;
    l *= f;                 /* 12 */
    n += f;                 /* 5 */
    u -= f;                 /* 4 */
    us *= f;                /* 22 */
    c8 += f;                /* 4 */
    s *= f;                 /* 17 */
    printf("%ld %d %lu %u %d %d\n", (long)l, n, (unsigned long)u, us, c8, s);
    l -= 1.5f;              /* 10 */
    l /= f;                 /* 4 */
    printf("%ld\n", (long)l);
    /* accepted */
    l = l * f;              /* 10 */
    f *= l;                 /* 25 */
    printf("%ld %d\n", (long)l, (int)f);
    return 0;
}
