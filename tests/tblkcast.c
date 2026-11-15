/* Equal-width word casts retain their signedness beside block long operands. */
#include <stdint.h>
#include <stdio.h>
static uint16_t g16 = 0x8000;
static uint8_t g1 = 15;
static int16_t signed_word = -32768;
static int32_t cast_compare(void)
{
    int32_t r = 0;
    {
        int32_t b = 0;
        if (((int16_t)g16) > b)
            r = 1;
    }
    return r;
}

static int32_t cast_shift_compare(void)
{
    int32_t r = 0;
    {
        int32_t b = 0;
        if (((int16_t)((uint16_t)(((uint16_t)1) << (g1 & 15)))) > b)
            r = 1;
    }
    return r;
}

static int32_t cast_sum(void)
{
    int32_t r;
    {
        int32_t b = 0;
        r = b + ((int16_t)g16);
    }
    return r;
}

/* correct: the local is declared at function scope */
static int32_t cast_compare_outer(void)
{
    int32_t r = 0;
    int32_t b = 0;
    if (((int16_t)g16) > b)
        r = 1;
    return r;
}


static int32_t unsigned_sum(void)
{
    int32_t r;
    { int32_t b = 1; r = b + (uint16_t)signed_word; }
    return r;
}
static int32_t signed_sum_reverse(void)
{
    int32_t r;
    { int32_t b = 1; r = (int16_t)g16 + b; }
    return r;
}
static int32_t same_width(void)
{
    int16_t r;
    { int16_t b = 1; r = (int16_t)g16 + b; }
    return r;
}
int main(void)
{
    printf("cast compare %ld\n", (long)cast_compare());
    printf("cast shift compare %ld\n", (long)cast_shift_compare());
    printf("cast sum %ld\n", (long)cast_sum());
    printf("cast compare outer %ld\n", (long)cast_compare_outer());
    printf("unsigned sum %ld\n", (long)unsigned_sum());
    printf("signed reverse %ld\n", (long)signed_sum_reverse());
    printf("same width %ld\n", (long)same_width());
    g16 = 32767; g1 = 14;
    printf("positive compare %ld\n", (long)cast_compare());
    printf("positive shift %ld\n", (long)cast_shift_compare());
    printf("positive sum %ld\n", (long)cast_sum());
    g16 = 65535;
    printf("minus one sum %ld\n", (long)cast_sum());
    return 0;
}
