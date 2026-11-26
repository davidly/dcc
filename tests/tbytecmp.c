/* Byte-fold equality and high-byte bounds: exhaustive word inputs, both
 * operand orders, signed controls, shared values, and observable barriers. */
#include <stdio.h>
static unsigned char bytes[2];
static volatile unsigned char vb;
static unsigned int calls;
static void change(void) { ++calls; bytes[0] ^= 1; }
static unsigned int bound_score(unsigned int x)
{
    unsigned int r=0;
    if(x < 256U) r |= 1U;
    if(x >= 16384U) r |= 2U;
    if(!(x >= 65280U)) r |= 4U;
    return r;
}
static int bounds(unsigned int x, unsigned int high)
{
    if (bound_score(x) != ((high < 1U ? 1U : 0U) |
                          (high >= 64U ? 2U : 0U) |
                          (high < 255U ? 4U : 0U))) return 4;
    if ((x < 256U) != (high < 1U) || (x >= 256U) != (high >= 1U) ||
        (x < 16384U) != (high < 64U) || (x >= 65280U) != (high >= 255U))
        return 1;
    if ((x < 257U) != (high == 0U || x == 256U)) return 2;
    if (((int)x < 256) != (high >= 128U || high == 0U)) return 3;
    return 0;
}
static int compare(unsigned int x, unsigned char want)
{
    unsigned char saved;
    bytes[0] = want; bytes[1] = want ^ 1;
    if (bytes[0] != (unsigned char)((x & 255U) ^ (x >> 8) ^ 90U)) return 1;
    if ((unsigned char)((x & 255U) ^ (x >> 8) ^ 90U) != bytes[0]) return 2;
    if (bytes[1] == (unsigned char)((x & 255U) ^ (x >> 8) ^ 90U)) return 3;
    if ((unsigned char)((x & 255U) ^ (x >> 8) ^ 90U) == bytes[1]) return 4;
    saved=bytes[0];
    if (saved != (unsigned char)((x & 255U) ^ (x >> 8) ^ 90U)) return 5;
    if ((unsigned int)saved + bytes[0] != (unsigned int)want * 2U) return 6;
    vb=want;
    if (vb != (unsigned char)((x & 255U) ^ (x >> 8) ^ 90U)) return 7;
    change();
    if (bytes[0] == (unsigned char)((x & 255U) ^ (x >> 8) ^ 90U)) return 8;
    return 0;
}
int main(void)
{
    unsigned int x=0, high=0, low=0;
    do {
        unsigned char want=(unsigned char)(low ^ high ^ 90U);
        if (bounds(x,high) || compare(x,want)) { puts("FAIL byte comparison"); return 1; }
        ++low; if(low==256U) { low=0; ++high; }
        ++x;
    } while(x);
    if(calls!=0 || (int)(unsigned char)255 == -1) { puts("FAIL signed control"); return 1; }
    puts("tbytecmp passed"); return 0;
}
