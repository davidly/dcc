/* Byte affine loops and demanded arithmetic must keep C promotions: a
 * wrapped byte never equals a promoted sum greater than 255. */
#include <stdint.h>
#include <stdio.h>

static void fill(uint8_t *p, uint8_t first)
{
    uint8_t i;
    for (i = 0; i < 11; ++i)
        p[i] = first + i;
}

static int check(const uint8_t *p, uint8_t first)
{
    uint8_t i;
    for (i = 0; i < 11; ++i)
        if (p[i] != first + i)
            return 0;
    return 1;
}

static int equal_sum(const uint8_t *p, uint8_t a, uint8_t b)
{
    if (*p != a + b)
        return 0;
    return 1;
}

static uint8_t low_product(uint32_t a, uint32_t b)
{
    return (uint8_t)(a * b + a - b);
}

static uint16_t low_negate(int32_t a, int32_t b)
{
    return (uint16_t)(~(a * b) - a);
}

static uint32_t full_product(uint32_t a, uint32_t b)
{
    uint32_t product = a * b;
    return product ^ (uint8_t)product;
}

static int byte_bounds(uint8_t a)
{
    return (a < 11) + 2 * (a <= 64) + 4 * (a > 127) +
           8 * (a >= 128) + 16 * (a == 255) + 32 * (a != 0);
}

static volatile uint32_t input;
static uint8_t low_volatile(void)
{
    return (uint8_t)(input * input + input);
}

int main(void)
{
    static const uint32_t values[] = {
        0UL, 1UL, 255UL, 256UL, 65535UL, 65536UL,
        0x80000000UL, 0xffffffffUL
    };
    uint8_t bytes[11];
    uint16_t i, j;
    for (i = 0; i < 256; ++i) {
        fill(bytes, (uint8_t)i);
        if (check(bytes, (uint8_t)i) != (i <= 245))
            return puts("FAIL carry");
        for (j = 0; j < 11; ++j)
            if (bytes[j] != (uint8_t)(i + j))
                return puts("FAIL fill");
        bytes[0] ^= 1;
        if (check(bytes, (uint8_t)i))
            return puts("FAIL mismatch");
        for (j = 0; j < 8; ++j) {
            uint16_t b = (uint8_t)values[j];
            bytes[0] = (uint8_t)(i + b);
            if (equal_sum(bytes, (uint8_t)i, (uint8_t)b) != (i + b <= 255))
                return puts("FAIL sum equality");
        }
        if (byte_bounds((uint8_t)i) !=
            ((i < 11) + 2 * (i <= 64) + 4 * (i > 127) +
             8 * (i >= 128) + 16 * (i == 255) + 32 * (i != 0)))
            return puts("FAIL bounds");
    }
    for (i = 0; i < 8; ++i) {
        for (j = 0; j < 8; ++j) {
            uint32_t a = values[i], b = values[j];
            /* Word operations independently compute the demanded low bits. */
            uint16_t product = (uint16_t)a * (uint16_t)b;
            if (low_product(a, b) != (uint8_t)(product + (uint16_t)a - (uint16_t)b))
                return puts("FAIL low product");
            /* Avoid signed long overflow in the signed test below. */
            if (i < 4 && j < 4 &&
                low_negate((int32_t)a, (int32_t)b) != (uint16_t)(~product - (uint16_t)a))
                return puts("FAIL low negate");
        }
    }
    if (full_product(0x10001UL, 0x10001UL) != 0x20000UL)
        return puts("FAIL shared full width");
    input = 0x123456ffUL;
    if (low_volatile() != 0)
        return puts("FAIL volatile");
    puts("tbyteaff passed");
    return 0;
}
