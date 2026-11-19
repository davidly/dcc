/* Regression for signed right shift after narrowing an unsigned product. */
#include <stdint.h>
#include <stdio.h>

struct S0 {
    int32_t m0;
    int16_t m1;
    int8_t m2;
};
struct S1 {
    int16_t m0 : 7;
    int16_t m1;
    int32_t m2[4];
    int16_t m3;
    int16_t m4 : 12;
    uint8_t m5;
};

static int16_t g3 = 0;

static void hex4(uint16_t v)
{
    int8_t k;
    for (k = 12; k >= 0; k -= 4)
        putchar("0123456789abcdef"[(v >> k) & 15]);
}

static void show(uint16_t id, uint32_t v)
{
    hex4(id);
    putchar(' ');
    hex4((uint16_t)(v >> 16));
    hex4((uint16_t)v);
    putchar('\n');
}

static uint32_t ck;

static void mix(uint32_t v)
{
    ck = ((ck << 5) | (ck >> 27)) ^ v;
    ck = ck + (uint32_t)0x9e3779b1;
}

static uint32_t state(void)
{
    uint8_t i, j;
    ck = 0;
    mix((uint32_t)g3);
    return ck;
}


static uint16_t h0(int32_t x0)
{
    return ((int16_t)((uint16_t)(g3 * ((uint16_t)1)))) >> 1;
}

static int32_t f0(int32_t x0)
{
    short l2 = h0(((int32_t)1));
    x0 = l2;
    --g3;
    return x0++;
}

int main(void)
{
    show(1, (uint32_t)f0(((int32_t)-32768)));
    show(16385, state());
    show(2, (uint32_t)f0(((int32_t)32767)));
    show(16386, state());
    return 0;
}
