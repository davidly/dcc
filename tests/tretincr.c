/* Preserve an incremented word in HL through a phi/return label.
 * Integer oracle seed 174848; locals retain the original allocation context. */
#include <stdint.h>
#include <stdio.h>

struct S { uint16_t bit : 1; int32_t wide; uint8_t byte;
           uint16_t word; int32_t tail; };
static struct S items[4] = {{0, -101, 255, 16, 65534}};
static uint8_t bytes[8] = {128, 63, 63, 254, 3, 1, 232, 2};
static uint16_t selected(void)
{
    static int16_t unused = -2;
    uint8_t byte = (uint16_t)1;
    int16_t word = items[0].word;
    uint16_t condition = (uint16_t)1;
    uint8_t *p = &bytes[(-187) & 3];
    if (condition)
        return (uint16_t)(1 ? (uint16_t)(word + (uint16_t)5) : 1);
    return -3911;
}
static uint16_t control(uint16_t word, uint8_t condition)
{
    if (condition)
        return word + (uint16_t)5;
    return 42;
}
int main(void)
{
    printf("%u %u %u %u\n", (unsigned)selected(),
           (unsigned)control(255, 1), (unsigned)control(65535U, 1),
           (unsigned)control(9, 0));
    return 0;
}
