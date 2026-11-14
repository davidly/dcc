/* Fold integer unary operators over constant logicals and nested binary
 * expressions; retain equivalent variable and single-unary controls. */
#include <stdint.h>
#include <stdio.h>

static int32_t negated(void)
{
    int32_t r;
    r = -(!(-128));
    r += -(!0);
    return r;
}

static int32_t complemented(void)
{
    int32_t r;
    r = ~(!15);
    r += ~(!0);
    return r;
}

static int16_t control(int16_t p)
{
    return ~(!p) + -(!p);
}

int main(void)
{
    int16_t n = 3;
    printf("negated %ld\n", (long)negated());
    printf("complemented %ld\n", (long)complemented());
    printf("logic %d %d %d %d\n", -(1 && 2), ~(1 || 2), -(0 || 0), ~(0 && 2));
    printf("binary %d %d %d %d %d %d\n", ~(-(7 >> 13)), -(~(7 >> 13)),
           -(-(7 >> 13)), ~(+(7 >> 13)), ~(-(8 - 8)), ~(-(1 > 2)));
    printf("control %d %d %d %d\n", control(0), control(3),
           (n > (~(1 && 2))), ~(3 > 2));
    return 0;
}
