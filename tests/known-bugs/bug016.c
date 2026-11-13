/* BUG-016: a local keeps the signedness of its initialiser instead of its
 * declared type. An unsigned 16-bit local initialised from a signed 16-bit
 * value is sign-extended when widened to 32 bits (and the reverse), and a
 * signed 32-bit local initialised from an unsigned expression divides as
 * unsigned. A constant initialiser and a same-width cast to the declared
 * type behave the same way. The code is correct when the value is assigned
 * in a separate statement. */
#include <stdint.h>
#include <stdio.h>

struct S { int8_t a; int16_t m3; int32_t m0; };
static struct S s = { -128, -31256, 20 };
static int8_t a6[2] = { -102, -86 };
static int32_t a3[2] = { -2, -3 };
static int16_t gs = -31256;
static uint16_t gu = 50000;

static int32_t unsigned_from_signed(void)
{
    uint16_t l = gs;
    return l;
}

static int32_t signed_from_unsigned(void)
{
    int16_t l = gu;
    return l;
}

static int32_t compared(void)
{
    uint16_t l = s.m3;
    return ((int32_t)1) <= l;
}

static int32_t from_cast(void)
{
    uint16_t l = (int16_t)s.a;
    return ((int32_t)l) > ((uint16_t)1);
}

static int32_t remainder_of(void)
{
    unsigned short l = (int16_t)a6[0];
    return l % s.m0;
}

static int32_t divided(uint16_t x)
{
    int32_t l = ((uint32_t)1) | a3[1];
    return x / l;
}

static int32_t from_constant(void)
{
    unsigned short l = -100;
    return l;
}

static int32_t same_width_cast(int16_t p)
{
    unsigned short l = (uint16_t)p;
    return l;
}

static int32_t same_width_cast_long(uint32_t p)
{
    int32_t l = (int32_t)p;
    return l % s.m0;
}

int main(void)
{
    printf("unsigned from signed %ld\n", (long)unsigned_from_signed());
    printf("signed from unsigned %ld\n", (long)signed_from_unsigned());
    printf("compared %ld\n", (long)compared());
    printf("from cast %ld\n", (long)from_cast());
    printf("remainder %ld\n", (long)remainder_of());
    printf("divided %ld\n", (long)divided(50000));
    printf("from constant %ld\n", (long)from_constant());
    printf("same-width cast %ld\n", (long)same_width_cast(-31256));
    printf("same-width cast long %ld\n", (long)same_width_cast_long(0xfffffffd));
    return 0;
}
