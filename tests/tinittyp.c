/* Declared signedness at promoted integer-local reads: mixed-signedness
 * initializers, same-width casts, widening, division/remainder and comparisons.
 * Nested-block float conversions retain the declaration's signedness, with
 * separate-assignment controls and a signed read before conversion. */
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


static uint32_t gu32 = 0xffffffbdUL;
static int32_t three = 3;
static float fg;

static unsigned long bits(float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    return v.u;
}

static int16_t h(void) { return -180; }

static void facets(void)
{
    {
        int16_t b = 0;
        b = (uint16_t)0xb840;
        printf("block remainder %ld\n", (long)(b % three));
    }
    {
        uint32_t b = (int32_t)1 - 78;
        fg = b;
        printf("block unsigned float %08lx\n", bits(fg));
    }
    {
        uint32_t b = (int32_t)1 - 78;
        float c = b;
        printf("block float local %08lx\n", bits(c));
    }
    {
        uint16_t b = h();
        fg = (float)b;
        printf("word float %08lx\n", bits(fg));
    }
    {
        uint16_t b = h();
        printf("word float compare %d\n", 8.1f > (float)b);
    }
    {
        int32_t t = (int32_t)gu32;
        fg = (float)t;
        printf("long cast float %08lx\n", bits(fg));
    }
    {
        int32_t t = gu32;
        printf("long signed read %ld\n", (long)t);
        fg = t;
        printf("long signed float %08lx\n", bits(fg));
    }
    {
        uint16_t b;
        b = gs;
        printf("separate unsigned %ld\n", (long)b);
    }
    {
        int16_t b;
        b = gu;
        printf("separate signed %ld\n", (long)b);
    }
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
    facets();
    return 0;
}
