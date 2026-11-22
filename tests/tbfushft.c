/* Unsigned word shifts after signed bit-field conversions, with division,
 * plain-word, unsigned-field and intermediate-widening controls. */
#include <stdint.h>
#include <stdio.h>

struct S { int16_t b:5; };
struct U { uint16_t b:5; };
static struct S s = {-12};
static struct U u = {20};
static int16_t plain = -12;

static int32_t shifted(void)
{
    return ((uint16_t)s.b) >> 4;
}

static int32_t divided(void)
{
    return ((uint16_t)s.b) / 16;
}

static int32_t local_shift(void)
{
    uint16_t x = s.b;
    return x >> 4;
}

static int32_t zero_shift(void)
{
    return ((uint16_t)s.b) >> 0;
}

static uint16_t word_result(void)
{
    return ((uint16_t)s.b) >> 4;
}

int main(void)
{
    printf("a %ld\n", (long)shifted());
    printf("b %ld\n", (long)divided());
    printf("c %ld\n", (long)local_shift());
    printf("d %ld\n", (long)zero_shift());
    printf("e %u\n", word_result());
    printf("scalar %u\n", ((uint16_t)plain) >> 4);
    printf("unsigned field %u\n", ((uint16_t)u.b) >> 4);
    printf("wide cast %u\n", ((uint16_t)(int32_t)s.b) >> 4);
    return 0;
}
