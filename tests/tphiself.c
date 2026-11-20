/* A loop which leaves a narrow local unchanged (l1 = l1 in a for loop)
 * carries its header phi around the backedge. In a function of 100+
 * blocks, the check whether that phi may reuse the local's named home
 * followed backedge phis recursively and never ended: dcc overflowed its
 * stack and crashed. The chain now only follows a phi to a later join.
 * Reduced from an integer oracle seed; it only has to compile and run. */
#include <stdint.h>
#include <stdio.h>
struct S0 {
    uint8_t m0;
    uint32_t m1;
    uint32_t m2;
    int8_t m3;
    int16_t m4[2];
    uint8_t m5[4];
};
struct S1 {
    uint8_t m0;
    int16_t m1;
    struct S0 m2;
    int16_t m3 : 15;
    uint16_t m4 : 8;
};
signed char g1 = -1;
uint8_t g3 = 5;
short g4 = 32767;
static int16_t g5 = 21930;
static uint16_t g6 = 0xffff;
static int32_t g7 = 623110291;
static int32_t g8 = -129;
static uint32_t g9 = 128;
static uint8_t a0[8] = { 7, 2, 124, 23, 0, 255, 1, 254 };
static int16_t a1[8] = { 294, -1, -32768, -25882, 128, -8277, 16, -79 };
uint16_t a2[4] = { 16762, 9029, 15517, 15 };
static int32_t a3[4] = { 41, -1, (-2147483647 - 1), 31 };
static uint32_t a4[8] = { 0xff00ff, 0x2d67384a, 291, 0, 8, 0xffffffff, 0xffffff7f, 0xffffff7f };
static int16_t a5[4][4] = { { 54, 5, 4785, -114 }, { 1000, 256, 128, -32768 }, { 21930, 1, 257, -2 }, { 16, 295, -1, 15 } };
static int8_t a6[8] = { 70, -100, -104, 69, -116, -21, -2, 2 };
static struct S0 s0 = { 255, 0xffffff43, 0x12345, -100, { 63, 50 }, { 255, 128, 7, 1 } };
static struct S1 s1 = { 0, 256, { 255, 0x80000000, 63, 31, { 222, -157 }, { 25, 127, 1, 69 } }, -7960, 79 };
static struct S1 s2[4] = { { 0, 5836, { 255, 5, 0, 5, { 6499, -29387 }, { 63, 175, 255, 85 } }, -10155, 150 }, { 155, 32767, { 156, 32767, 0xaa55, -46, { 255, -32027 }, { 0, 236, 255, 7 } }, 12445, 222 }, { 128, 2, { 100, 21930, 3, 16, { 1, 255 }, { 0, 164, 8, 255 } }, -4946, 205 }, { 85, -94, { 112, 32766, 31, -1, { 127, 32766 }, { 15, 97, 255, 94 } }, -8128, 36 } };
static int16_t h1(signed char x0, char x1)
{
    return x0 + x1;
}
static int16_t (*h1_p)(signed char, char) = h1;
static uint8_t h0(uint8_t x0, uint8_t x1, int32_t x2, int32_t x3)
{
    return x0 ^ x1 ^ (uint8_t)x2 ^ (uint8_t)x3;
}
static int8_t k0(uint32_t *x0, int16_t x1, int16_t x2)
{
    return (int8_t)(*x0 + x1 + x2);
}
static int16_t f1(int32_t x0, uint8_t x1)
{
    unsigned short l1 = (int32_t)(((uint32_t)a1[1]) + ((uint32_t)a3[2]));
    int16_t l2 = l1;
    int16_t l3 = ((uint16_t)sizeof(g9)) >= ((int32_t)65535);
    unsigned char l4 = (*h1_p)(a0[a0[((int32_t)-929403231) & 7] & 7], x1);
    unsigned short l5 = s2[g6 & 3].m3 + l2;
    unsigned short l6 = a1[2] - x0;
    char d8 = 0;
    unsigned char i10 = 0;
    int16_t i20 = 0;
    uint16_t n21 = 0;
    if (((uint16_t)(((a2[s0.m1 & 3] / (s2[1].m2.m1 | 1)) >= ((uint16_t)((uint16_t)((((uint16_t)a1[6]) > ((uint16_t)0xffde)) ? s2[2].m2.m4[0] : sizeof(uint16_t))))) ? ((uint16_t)g3) : (l2 | s0.m0))) > ((-s2[a4[((int32_t)16711935) & 7] & 3].m2.m2) * (-x1))) {
        switch (h0((a3[1] - g5), ((uint16_t)(l6 + l1)), (256 >> (((uint32_t)1676305195) & 15)), (a0[4] * a6[((int32_t)-3) & 7])) & 15) {
        case 12:
        case 0:
            l1 = ((uint16_t)((h1(s0.m4[0], s2[x0 & 3].m4) >= ((g1 < x0) ? x0 : x0)) ? ((uint16_t)(l5 * l3)) : h0(a5[1][3], 2366, ((uint32_t)1), l6)));
            for (uint16_t i7 = 7; i7 > ((uint16_t)2); --i7) {
                l1 = l1;
            }
            if (l2 != (*h1_p)((~15), h0(sizeof(g8), s1.m4, s0.m4[a1[sizeof(g4) & 7] & 1], l6))) {
                l2 = k0((&a4[d8]), (((((uint16_t)8) <= a2[i10 & 3]) && (((uint16_t)l4) != sizeof(a1))) ? i10 : ((uint32_t)0xfffffffe)), (s0.m3 < a5[d8][d8]));
            }
            switch (((s0.m3 - l2) + ((int32_t)8)) % 4) {
            case 2:
            case -2:
                switch ((((uint32_t)65536) ^ s0.m4[l5 & 1]) % 4) {
                }
            case 0:
                g6 = (s1.m2.m2 || a0[d8]);
            }
        }
    } while ((++d8) < 3);
    if (l4 != s0.m5[2]) {
        for (i20 = 2; ((uint16_t)i20) < n21; i20++) {
            unsigned short b22 = (uint16_t)(((uint16_t)(l5 * a6[a4[((int32_t)32766) & 7] & 7])) - ((uint16_t)((!s2[3].m2.m3) ? l4 : sizeof(a1))));
            if ((((uint32_t)s2[2].m0) + ((uint32_t)a5[s2[s0.m4[1] & 3].m3 & 3][((uint32_t)0xb54996e9) & 3])) == l5) {
                switch (s0.m0 & 7) {
                default:
                case 1:
                case 5:
                    x1 += ((int32_t)25);
                }
            } else {
                switch ((int8_t)(((int32_t)2147483647) - l1)) {
                case 0:
                case -1:
                case -2:
                default:
                    g5 = a3[0];
                }
            }
        }
    } else {
    }
    if ((((l3 <= s1.m0) && (sizeof(g5) >= ((uint16_t)l2))) && (a6[1] <= g3)) && (a6[s1.m2.m4[1] & 7] == ((int32_t)-1005025337))) goto L1;
L1:;
    g3 = ((((uint16_t)(((uint16_t)(l6 ^ s0.m4[1])) & (x1 || g7))) / ((uint16_t)5)) - (((a3[x0 & 3] + a1[7]) + ((int8_t)x0)) & (~(((int32_t)255) + a2[g4 & 3]))));
    int16_t w15 = 0;
    while ((w15--) != 0) {
    }
    return l2;
}

int main(void)
{
    f1(1, 2);
    printf("tphiself passed\n");
    return 0;
}
