/* Loop-invariant BC hoisting must respect EXX bank switches. The first
 * loop has a constant bound; the second loads its bound from a byte array.
 * Unused parameters and reads retain the register placement of the oracle
 * reproducers (integer seeds 212413 and 228464). */
#include <stdint.h>
#include <stdio.h>

struct S { int16_t value; uint16_t a, b; };
static int32_t count1 = 293;
static uint32_t count2 = 0xaa55;
static uint8_t bytes[8] = { 15, 198, 69, 213, 254, 2, 255, 157 };
static uint8_t bounds[4][4] = {
    { 255, 103, 253, 118 }, { 1, 153, 255, 108 },
    { 255, 0, 15, 137 }, { 7, 128, 69, 255 }
};
static uint16_t words[4] = { 9029, 0xfc90, 0, 129 };
static struct S object = { 32767, 0xff7f, 1 };

static uint32_t constant_bound(int32_t x0, unsigned char x1, int32_t x2)
{
    unsigned short i = 0;
    uint16_t n = 0;
    n = (int32_t)1;
    for (i = 0; i < n; i += 1) {
        ++count1;
        bytes[1] = 31;
    }
    return 1;
}

static void loaded_bound(struct S *p, int32_t x)
{
    int16_t local = x;
    int16_t i = 0;
    uint16_t n = 0;
    (int32_t)p->value;
    words[local & 3];
    n = bounds[1][((int32_t)31) & 3];
    for (i = 0; (uint16_t)i < n; ++i)
        ++count2;
}

static uint16_t control(uint16_t n)
{
    uint16_t i, sum = 0;
    for (i = 0; i < n; ++i)
        sum += i;
    return sum;
}

int main(void)
{
    constant_bound((uint16_t)32766, 1, (int32_t)1);
    printf("constant %ld %u\n", (long)count1, bytes[1]);
    loaded_bound(&object, (int32_t)1);
    printf("loaded %lu\n", (unsigned long)count2);
    count1 = 65535;
    constant_bound(0, 0, 0);
    count2 = 65535;
    bounds[1][3] = 1;
    loaded_bound(&object, 0);
    printf("carry %ld %lu\n", (long)count1, (unsigned long)count2);
    bounds[1][3] = 0;
    loaded_bound(&object, 0);
    printf("zero %lu\n", (unsigned long)count2);
    printf("control %u %u %u\n", control(0), control(1), control(5));
    return 0;
}
