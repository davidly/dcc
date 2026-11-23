/* Target-width unary constant conditions must preserve reachable returns.
 * Integer oracle seed 144144, with word/long, signed and nested controls. */
#include <stdint.h>
#include <stdio.h>

static int8_t long_complement(void)
{
    if (~(uint32_t)0xffffffffUL)
        return 9;
    return (int32_t)1;
}
static int16_t word_complement(void)
{
    if ((uint16_t)~(uint16_t)0xffffU)
        return 9;
    return 2;
}
static int16_t nested(void)
{
    if (!~(uint32_t)0xffffffffUL)
        return 3;
    return 9;
}
static int16_t signed_complement(void)
{
    if (~(int32_t)-1)
        return 9;
    return 4;
}
static int16_t nonzero(void)
{
    if (~(uint32_t)0xfffffffeUL)
        return 5;
    return 9;
}
int main(void)
{
    printf("%d %d %d %d %d\n", (int)long_complement(),
           (int)word_complement(), (int)nested(),
           (int)signed_complement(), (int)nonzero());
    return 0;
}
