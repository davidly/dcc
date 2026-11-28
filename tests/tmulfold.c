/* A folded narrow constant feeding a long multiply (typed-scope fuzz seed
 * 3000026). `t` folds to (unsigned)0xfffe, but the constant kept the
 * immediate -2: the multiply-plan check accepted -2 while the emitter
 * widened it to 65534, so dcc failed with "invalid immediate long
 * multiply plan". */
#include <stdio.h>

static unsigned long scale(unsigned long l, int k)
{
    if (k) {
        unsigned t = (2147483646L - 1);
        ++t;
        l *= t;
    }
    return l;
}

static long scale_signed(long l, int k)
{
    if (k) {
        unsigned t = (2147483646L - 1);
        ++t;
        l = t * l;
    }
    return l;
}

int main(void)
{
    printf("%lu %lu %lu\n", scale(3UL, 1), scale(70000UL, 2), scale(9UL, 0));
    printf("%ld %ld\n", scale_signed(3L, 1), scale_signed(-5L, 1));
    return 0;
}
