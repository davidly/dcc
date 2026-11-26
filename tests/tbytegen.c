/* Low-byte demand must preserve signed extension, shifts, parameter homes,
 * nested operand saves, and observable calls/volatile reads. */
#include <stdio.h>
static int effects;
static volatile unsigned int observed;
static unsigned char hash(unsigned int x) { return (unsigned char)((x & 255U) ^ (x >> 8) ^ 90U); }
static signed char signed_hash(unsigned int x) { return (signed char)((x & 255U) ^ (x >> 8) ^ 90U); }
static unsigned char nested(unsigned int x, unsigned int y, unsigned int z)
{ return (unsigned char)(x ^ (y | z)); }
static unsigned char masks(unsigned int x, unsigned int y)
{ return (unsigned char)((x & 0xff55U) | (y ^ 0x80aaU)); }
static unsigned char sx(signed char x) { return (unsigned char)(((int)x) >> 8); }
static unsigned char ux(unsigned char x) { return (unsigned char)(((unsigned int)x) >> 8); }
static unsigned char truncated(unsigned int x) { return (unsigned char)(((unsigned char)x) >> 8); }
static unsigned char truth(unsigned int x) { return (unsigned char)(x != 0); }
static unsigned char called(unsigned int x) { ++effects; return hash(x); }
static unsigned char watched(void) { return (unsigned char)observed; }
int main(void)
{
    unsigned int x = 0;
    do {
        unsigned int want = ((x % 256U) ^ (x / 256U)) ^ 90U;
        int signed_want = want < 128U ? (int)want : (int)want - 256;
        if (hash(x) != want || signed_hash(x) != signed_want ||
            nested(x, 0x1234U, 0x5678U) != (unsigned char)(x ^ 0x567cU) ||
            masks(x, 0x1234U) != (unsigned char)((x & 85U) | 158U) ||
            truncated(x) != 0 || truth(x) != (x != 0)) {
            printf("FAIL byte %u\n", x);
            return 1;
        }
    } while (++x != 0);
    if (sx(-128) != 255U || sx(127) != 0 || ux(255U) != 0 ||
        called(0) != 90U || called(1) != 91U || effects != 2) {
        puts("FAIL extension/effects"); return 1;
    }
    observed = 0xabcdU;
    if (watched() != 205U) { puts("FAIL volatile"); return 1; }
    puts("tbytegen passed");
    return 0;
}
