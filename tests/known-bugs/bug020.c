/* BUG-020: after a switch statement, a relational comparison between a
 * variable and the 32-bit product, quotient or remainder of a 32-bit
 * parameter stops the compile with "MIR emission is required". The same
 * comparison compiles with no switch before it, with ==, with the operands
 * the other way round, or with the parameter copied to a local first. */
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

int main(void)
{
    printf("remainder >= %ld %ld\n", (long)remainder_ge(70000), (long)remainder_ge(-70000));
    printf("quotient < %ld %ld\n", (long)quotient_lt(70000), (long)quotient_lt(300));
    printf("product >= %ld %ld\n", (long)product_ge(70000), (long)product_ge(-70000));
    printf("member >= %ld %ld\n", (long)member_ge(70000), (long)member_ge(70001));
    printf("as value %ld %ld\n", (long)as_value(70000), (long)as_value(118));
    printf("switch arms taken %u\n", (unsigned)hits);
    return 0;
}
