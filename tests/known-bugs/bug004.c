/* BUG-004 remaining byte-local facet, reduced from integer seed 133.
 * Word locals and nested-block long assignments now pass (tests/tlogblk.c).
 * Forwarding this signed-byte logical result still leaves a wide store's
 * high word uninitialized; the checksum observes the incorrect array value. */
#include <stdint.h>
#include <stdio.h>

struct S0 {
    int16_t m0;
    int32_t m1;
    int32_t m2;
    uint32_t m3;
    int16_t m4 : 15;
    uint32_t m5[4];
};
struct S1 {
    uint16_t m0 : 3;
    int16_t m1 : 2;
    int16_t m2;
    uint16_t m3;
};

static uint32_t a4[8] = { 0xa279b3d8, 0xffff7fff, 0xaa55, 0xffffff80, 0x80000001, 0x8000, 0x95b10ca, 0x8001 };

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
    for (i = 0; i < 8; i++)
        mix((uint32_t)a4[i]);
    return ck;
}


static int16_t f0(void)
{
    signed char l2 = 1 && ((uint16_t)1);
    a4[3] = l2;
    return 1;
}

int main(void)
{
    show(1, (uint32_t)f0());
    show(16385, state());
    return 0;
}
