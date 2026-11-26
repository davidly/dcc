/* Boundary controls for byte/word sums, signed-bound narrowing, division,
 * constant outer loops, and unsigned-word additions to long accumulators. */
#include <stdio.h>
#include <stdint.h>
static unsigned char byte;
static volatile unsigned char observed;
static int equal32(unsigned char *p, unsigned char x)
{ if (*p == x + 32) return 1; return 0; }
static int unequal32(unsigned char *p, unsigned char x)
{ if (x + 32 != *p) return 1; return 0; }
static int equal255(unsigned char *p, unsigned char x)
{ if (*p == x + 255) return 1; return 0; }
static int limits(int x, int y, int z)
{
    if ((long)-2147483648L >= x) return 1;
    if (((long)x >= -32768L) != 1 || ((long)x > 32767L) != 0) return 2;
    return y != z;
}
static uint32_t wide(uint32_t a, unsigned int b, unsigned int c)
{ return a + (unsigned long)b + (unsigned long)c; }
#define DIVIDE(D) do { \
    unsigned int q = v / (D), r = v % (D); \
    if (r >= (D) || (unsigned long)q * (D) + r != v) return 1; \
} while (0)
static int divide(unsigned int v)
{
    DIVIDE(3U); DIVIDE(255U); DIVIDE(256U); DIVIDE(257U);
    DIVIDE(1000U); DIVIDE(65535U);
    return 0;
}
static int rows(void)
{
    int a[2][20], i, j, total = 0;
    for (i = 0; i < 2; ++i)
        for (j = 0; j < 20; ++j) a[i][j] = i * 20 + j;
    for (i = 0; i < 2; ++i)
        for (j = 0; j < 20; ++j) total += a[i][j];
    if (i != 2 || j != 20 || total != 780) return 1;
    total = 0;
    for (i = 0; i < 2; ++i)
        for (j = 0; j < 20; ++j) {
            if (j == 2) break;
            if (j == 0) continue;
            ++total;
        }
    return i != 2 || j != 2 || total != 2;
}
static int boolean_sum(void)
{
    unsigned long sum=0;
    unsigned int i;
    for(i=0;i<12;++i) sum += (unsigned int)(i != 3);
    return sum != 11UL;
}
int main(void)
{
    static unsigned int edges[] = {256U,257U,511U,512U,999U,1000U,32767U,32768U,65534U,65535U};
    static int signed_edges[] = {-32767-1,-32767,-256,-20,-13,-1,0,1,255,32767};
    unsigned int x, k;
    for (x = 0; x < 256U; ++x) {
        byte = (unsigned char)(x + 32);
        if (equal32(&byte,(unsigned char)x) != (x < 224U) ||
            unequal32(&byte,(unsigned char)x) != (x >= 224U)) return 1;
        byte ^= 1;
        if (equal32(&byte,(unsigned char)x) || !unequal32(&byte,(unsigned char)x)) return 2;
        byte = (unsigned char)(x + 255);
        if (equal255(&byte,(unsigned char)x) != (x == 0)) return 3;
        observed = byte;
        if ((observed == x + 255) != (x == 0)) return 4;
        if (divide(x)) return 5;
    }
    for (k = 0; k < 10; ++k)
        if (divide(edges[k]) || limits(signed_edges[k],7,7)) return 6;
    if (limits(-13,7,8) != 1 || rows() || boolean_sum()) return 7;
    if (wide(0xffff0000UL,65535U,2U) != 1UL ||
        wide(0x7fff0000UL,65535U,65535U) != 0x8000fffeUL ||
        wide(0xffffffffUL,1U,0U) != 0UL) return 8;
    puts("tgenedge passed");
    return 0;
}
