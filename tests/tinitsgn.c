/* Declared signedness through positive long initializers, division,
 * remainder, and chained word shifts, with separate-assignment controls. */
#include <stdint.h>
#include <stdio.h>

static int32_t divided(int32_t x)
{
    int32_t d = (uint32_t)4;
    return x / d;
}

static int32_t signed_divisor(int32_t x)
{
    int32_t d = (int32_t)4;
    return x / d;
}

static int32_t separate_divisor(int32_t x)
{
    int32_t d = 4;
    d = (uint32_t)4;
    return x / d;
}

static int32_t divided_negative(void)
{
    int32_t x = (uint32_t)88;
    return x / ((int32_t)-80);
}

static int32_t signed_numerator(void)
{
    int32_t x = (int32_t)88;
    return x / ((int32_t)-80);
}

static int32_t remainder(void)
{
    int32_t x = (uint32_t)88;
    return x % ((int32_t)-80);
}

static int16_t shift_chain(uint16_t x)
{
    int16_t l = x;
    return (l >> 2) >> 13;
}

static int16_t shift_word(uint16_t x)
{
    int16_t l = x;
    return l >> 2;
}

static int16_t separate_word(uint16_t x)
{
    int16_t l = 0;
    l = x;
    return (l >> 2) >> 13;
}

static int16_t signed_word(int16_t x)
{
    int16_t l = x;
    return (l >> 2) >> 13;
}

int main(void)
{
    printf("a %ld\n", (long)divided(-128));
    printf("b %ld\n", (long)signed_divisor(-128));
    printf("c %ld\n", (long)separate_divisor(-128));
    printf("d %ld\n", (long)divided_negative());
    printf("e %ld\n", (long)signed_numerator());
    printf("f %ld\n", (long)remainder());
    printf("word chain %d\n", shift_chain((uint16_t)-67));
    printf("word single %d\n", shift_word((uint16_t)-67));
    printf("word separate %d\n", separate_word((uint16_t)-67));
    printf("word signed %d\n", signed_word(-67));
    return 0;
}
