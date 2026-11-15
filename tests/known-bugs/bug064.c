/* BUG-064: this bounded recursion (r0 returns once (x0 & 7) == 0 and
 * otherwise recurses on (x0 & 7) - 1, reached through the h1_p function
 * pointer) grows the Z80 stack without bound, so the program overflows any
 * stack limit instead of printing the one result line. The host prints
 * `0010 00000000`. Reduced from clang-oracle-fuzz.py integer seed 261, call
 * 16; the empty nested if/else block in f5 is still required to reproduce. */
#include <stdint.h>
#include <stdio.h>

struct S0 {
    int8_t m0;
    uint16_t m1;
    uint8_t m2;
    uint16_t m3 : 7;
    uint8_t m4;
    uint16_t m5 : 7;
};
struct S1 {
    int16_t m0;
    int8_t m1;
    int16_t m2 : 13;
};

static signed char g0 = -1;
static signed char g1 = -24;
uint8_t g2 = 1;
static short g3 = 220;
short g4 = 100;
unsigned short g5 = 0xffff;
int32_t g6 = -58;
static uint32_t g7 = 0;
static uint8_t a0[8] = { 254, 127, 126, 255, 127, 2, 220, 102 };
int16_t a1[8] = { 256, -1, 255, 2, -21931, 100, 3, 29272 };
static uint16_t a2[4] = { 257, 1, 255, 100 };
int32_t a3[4] = { 65534, 1540452132, 7, 16711935 };
static uint32_t a4[8] = { 0x50943ff1, 0xd69f6fa6, 257, 257, 24, 128, 0xf6c7bc55, 0x1289b089 };
static uint8_t a5[4][4] = { { 254, 125, 1, 116 }, { 128, 204, 69, 70 }, { 225, 255, 8, 115 }, { 156, 85, 128, 85 } };
static int8_t a6[8] = { 69, -27, 1, 101, -30, 1, -1, 68 };
static struct S0 s0 = { 0, 16061, 255, 48, 222, 19 };
static struct S1 s1 = { -2, 31, -3534 };
static struct S1 s2[4] = { { 100, -1, 1565 }, { 0, 3, -4009 }, { 0, -127, 2174 }, { 1, 8, 595 } };

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
    mix((uint32_t)g0);
    mix((uint32_t)g1);
    mix((uint32_t)g2);
    mix((uint32_t)g3);
    mix((uint32_t)g4);
    mix((uint32_t)g5);
    mix((uint32_t)g6);
    mix((uint32_t)g7);
    for (i = 0; i < 8; i++)
        mix((uint32_t)a0[i]);
    for (i = 0; i < 8; i++)
        mix((uint32_t)a1[i]);
    for (i = 0; i < 4; i++)
        mix((uint32_t)a2[i]);
    for (i = 0; i < 4; i++)
        mix((uint32_t)a3[i]);
    for (i = 0; i < 8; i++)
        mix((uint32_t)a4[i]);
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            mix((uint32_t)a5[i][j]);
    for (i = 0; i < 8; i++)
        mix((uint32_t)a6[i]);
    {
        mix((uint32_t)s0.m0);
        mix((uint32_t)s0.m1);
        mix((uint32_t)s0.m2);
        mix((uint32_t)s0.m3);
        mix((uint32_t)s0.m4);
        mix((uint32_t)s0.m5);
    }
    {
        mix((uint32_t)s1.m0);
        mix((uint32_t)s1.m1);
        mix((uint32_t)s1.m2);
    }
    for (i = 0; i < 4; i++)
        {
            mix((uint32_t)s2[i].m0);
            mix((uint32_t)s2[i].m1);
            mix((uint32_t)s2[i].m2);
        }
    return ck;
}

static uint32_t r0(uint8_t x0, uint32_t x1);
static int8_t h1(unsigned char x0, char x1);
static int8_t (*h1_p)(unsigned char, char) = h1;

static uint32_t r0(uint8_t x0, uint32_t x1)
{
    if ((x0 & 7) == 0) {
        return x1;
    }
    return r0(((uint8_t)((x0 & 7) - 1)), (a6[5] | ((s2[3].m1 >= s2[0].m0) ? a3[1] : a1[x1 & 7])));
}

static int8_t h1(unsigned char x0, char x1)
{
    short l1 = r0(x1, x0);
    return ((uint16_t)(x0 + x0)) < a2[3];
}

static uint32_t k0(unsigned short x0, struct S1 x1, uint8_t x2, uint16_t x3)
{
    return (uint16_t)((uint16_t)(x0 & (-14924)));
}

static int32_t f5(signed char x0, int32_t x1, int16_t *x2)
{
    short l1 = s2[3].m2 * 1;
    int8_t l2 = (int32_t)(((uint32_t)(*x2)) - ((uint32_t)x1));
    int32_t *p3 = &a3[2];
    if (((uint16_t)a6[(-((uint32_t)8)) & 7]) < ((uint16_t)((32767 | l2) ^ (((uint16_t)l1) / ((uint16_t)0xfeed))))) {
        l1 ^= k0((g7 + (((uint32_t)15) - a2[0])), s2[3], ((l2 & ((uint32_t)255)) - ((*p3) >> (x0 & 31))), (*h1_p)((*h1_p)(s2[l2 & 3].m1, g0), (a0[((int32_t)127) & 7] * a3[3])));
        {
            if (((uint32_t)65537) << 13) {
            } else {
            }
        }
    }
    l2 = k0((-(1000 + g2)), s2[(100 ^ x0) & 3], g5, (!((uint8_t)a3[l1 & 3])));
    a3[2] = (*h1_p)(((int32_t)(((uint32_t)((((*x2) == g1) && ((x1 == (*p3)) && (166 <= s2[2].m1))) ? x1 : p3[s0.m2 & 1])) << ((l1++) & 31))), ((g1 * ((int32_t)32767)) + (-a5[3][2])));
    return (a6[0] && a6[4]) & (*h1_p)(x1, a1[(-16657) & 7]);
}

int main(void)
{
    show(16, (uint32_t)f5(100, ((int32_t)2147483647), (&a1[2])));

    return 0;
}
