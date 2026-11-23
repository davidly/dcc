/* Byte update values must wrap before fused 16-by-16 widening multiplies.
 * Integer oracle seed 120086; exercise both operand orders and signed bytes. */
#include <stdint.h>
#include <stdio.h>

static uint16_t factor = 257;
static uint32_t prefix(uint8_t x)
{
    if (x = 0) {
    } else {
    }
    return (uint32_t)factor * --x;
}
static uint32_t reverse(uint8_t x)
{
    return --x * (uint32_t)factor;
}
static int32_t signed_prefix(int8_t x)
{
    return (int32_t)(int16_t)factor * --x;
}
static uint32_t increment(uint8_t x)
{
    return (uint32_t)factor * ++x;
}
static uint32_t postfix(uint8_t x)
{
    return (uint32_t)factor * x--;
}
int main(void)
{
    printf("%lu %lu %ld\n", (unsigned long)prefix(1),
           (unsigned long)reverse(0), (long)signed_prefix(0));
    printf("%lu %lu %lu\n", (unsigned long)increment(255),
           (unsigned long)increment(4), (unsigned long)postfix(3));
    return 0;
}
