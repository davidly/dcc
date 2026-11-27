/* Sparse long constant products, both operand orders and all carry lanes.
 * The oracle only uses unsigned shifts/addition with a runtime multiplier. */
#include <stdint.h>
#include <stdio.h>

#define PRODUCT(N,K) \
    static uint32_t r##N(uint32_t x) { return x * (K); } \
    static uint32_t l##N(uint32_t x) { return (K) * x; }
PRODUCT(0,0UL) PRODUCT(1,1UL) PRODUCT(2,2UL) PRODUCT(3,3UL)
PRODUCT(4,5UL) PRODUCT(5,9UL) PRODUCT(6,255UL) PRODUCT(7,257UL)
PRODUCT(8,1000UL) PRODUCT(9,65535UL) PRODUCT(10,65537UL)
PRODUCT(11,0x7fffffffUL) PRODUCT(12,0x80000000UL)
PRODUCT(13,0xffffff01UL) PRODUCT(14,0xfffffc18UL)
PRODUCT(15,0xffffffffUL) PRODUCT(16,0x01010101UL)
PRODUCT(17,0x12345678UL) PRODUCT(18,0x01000001UL)
PRODUCT(19,0x000100ffUL) PRODUCT(20,0xffff0001UL)
PRODUCT(21,0x80800001UL)

struct Product {
    uint32_t constant;
    uint32_t (*right)(uint32_t);
    uint32_t (*left)(uint32_t);
};
#define ENTRY(N,K) { K, r##N, l##N }
static const struct Product products[] = {
    ENTRY(0,0UL), ENTRY(1,1UL), ENTRY(2,2UL), ENTRY(3,3UL),
    ENTRY(4,5UL), ENTRY(5,9UL), ENTRY(6,255UL), ENTRY(7,257UL),
    ENTRY(8,1000UL), ENTRY(9,65535UL), ENTRY(10,65537UL),
    ENTRY(11,0x7fffffffUL), ENTRY(12,0x80000000UL),
    ENTRY(13,0xffffff01UL), ENTRY(14,0xfffffc18UL),
    ENTRY(15,0xffffffffUL), ENTRY(16,0x01010101UL),
    ENTRY(17,0x12345678UL), ENTRY(18,0x01000001UL),
    ENTRY(19,0x000100ffUL), ENTRY(20,0xffff0001UL),
    ENTRY(21,0x80800001UL)
};

static uint32_t reference(uint32_t a, uint32_t b)
{
    uint32_t result = 0;
    while (b) {
        if (b & 1UL) result += a;
        a <<= 1;
        b >>= 1;
    }
    return result;
}

static volatile uint32_t observed;
static unsigned calls;
static uint32_t once(uint32_t x) { ++calls; return x; }
static int32_t signed_product(int32_t x) { return x * -255L; }

int main(void)
{
    static const uint32_t values[] = {
        0UL,1UL,255UL,65535UL,65536UL,0x7fffffffUL,0x80000000UL,
        0xffffffffUL,0x12345678UL,0xabcdef01UL
    };
    unsigned i, j;
    uint32_t seed = 0x12345678UL;
    for (i = 0; i < sizeof(products)/sizeof(products[0]); ++i) {
        for (j = 0; j < 20; ++j) {
            uint32_t x, expected;
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            x = j < 10 ? values[j] : seed;
            expected = reference(x, products[i].constant);
            if (products[i].right(x) != expected || products[i].left(x) != expected)
                return puts("FAIL long constant product");
        }
    }
    for (i = 0; i < 200; ++i) {
        int32_t x = (int32_t)i - 100L;
        int32_t expected = (int32_t)reference((uint32_t)(x < 0 ? -x : x),255UL);
        if (x >= 0) expected = -expected;
        if (signed_product(x) != expected)
            return puts("FAIL signed product");
    }
    observed = 0xabcdef01UL;
    if ((uint32_t)(observed * 255UL) != reference(0xabcdef01UL,255UL) ||
        (uint32_t)(once(0xabcdef01UL) * 257UL) != reference(0xabcdef01UL,257UL) || calls != 1)
        return puts("FAIL evaluation effects");
    puts("tlcsdmul passed");
    return 0;
}
