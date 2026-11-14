/* Comparisons against compound 32-bit and float right operands after
 * switches, loop-bearing branches and braced early returns. Covers long
 * products/quotients/remainders, narrow globals/members, float parameters,
 * mixed integer constants, and both taken and untaken results. */
#include <stdint.h>
#include <stdio.h>

struct S { uint8_t b[4]; };
static struct S s = { { 1, 255, 127, 3 } };
static uint8_t g8 = 3;
static int16_t g16 = 3;
static int32_t g32 = 3;
static int16_t sel = 5;
static uint16_t hits;

static int32_t remainder_ge(int32_t x)
{
    switch (sel) {
    case 5:
        hits++;
    }
    if (g16 >= (x % 115))
        return 1;
    return 0;
}

static int32_t quotient_lt(int32_t x)
{
    switch (sel) {
    case 0:
        hits++;
        break;
    default:
        break;
    }
    if (g8 < (x / 115))
        return 1;
    return 0;
}

static int32_t product_ge(int32_t x)
{
    switch (sel) {
    case 5:
        hits++;
    }
    if (g32 >= (x * 115))
        return 1;
    return 0;
}

static int32_t member_ge(int32_t x)
{
    switch (sel) {
    case 5:
        hits++;
    }
    if (s.b[3] >= (x % g16))
        return 1;
    return 0;
}

static int32_t as_value(int32_t x)
{
    switch (sel) {
    case 5:
        hits++;
    }
    return g16 >= (x % 115);
}

static int16_t n = 1;
static float fg1 = 3.0f, fg2 = 7.0f;
static int after_loop(float x) {
    if (n) { while (n) n--; } else { }
    return fg2 < x * fg1;
}
static int after_return(float x) {
    float l = -2.0f;
    if (x) { return 1; }
    return 1.0f >= (x - l);
}
static int mixed_rhs(float x) {
    int16_t k = 3;
    if (n) { while (k > 0) k = 0; } else { }
    return 7.0f != (x + 28);
}

int main(void)
{
    printf("remainder >= %ld %ld\n", (long)remainder_ge(70000), (long)remainder_ge(-70000));
    printf("quotient < %ld %ld\n", (long)quotient_lt(70000), (long)quotient_lt(300));
    printf("product >= %ld %ld\n", (long)product_ge(70000), (long)product_ge(-70000));
    printf("member >= %ld %ld\n", (long)member_ge(70000), (long)member_ge(70001));
    printf("as value %ld %ld\n", (long)as_value(70000), (long)as_value(118));
    printf("switch arms taken %u\n", (unsigned)hits);
    printf("loop %d %d\n", after_loop(2.0f), after_loop(3.0f));
    printf("return %d %d\n", after_return(0.0f), after_return(1.0f));
    printf("mixed %d %d\n", mixed_rhs(-21.0f), mixed_rhs(2.0f));
    return 0;
}
