/* Byte compound left shifts compute in promoted int, then narrow the
 * stored and returned value; cover memory/constant starts and byte counts. */
#include <stdint.h>
#include <stdio.h>

static uint8_t a1[4] = { 200, 2, 3, 4 };
static uint8_t count = 5;

static uint8_t from_array(void)
{
    uint8_t x = a1[0];
    x <<= count;
    return x;
}

static uint8_t from_constant(void)
{
    uint8_t x = 200;
    x <<= count;
    return x;
}

static uint8_t after_add(void)
{
    uint8_t x = a1[0];
    x += count;
    x <<= count;
    return x;
}

static uint8_t local_count(void)
{
    uint8_t x = a1[0];
    uint8_t n = count;
    x <<= n;
    return x;
}

static uint8_t shifted(uint8_t x, uint8_t n)
{
    x <<= n;
    return x;
}

static int edge_counts(void)
{
    static uint8_t values[6] = { 0, 1, 127, 128, 200, 255 };
    uint8_t n, x;
    int i;
    for (n = 0; n < 8; ++n)
        for (i = 0; i < 6; ++i)
            if ((uint16_t)shifted(values[i], n) !=
                (uint8_t)((uint16_t)values[i] << n))
                return 0;
    /* Keep the promoted signed-int shift representable on the target. */
    for (n = 8; n < 16; ++n) {
        x = (uint8_t)(32767u >> n);
        if ((uint16_t)shifted(x, n) != (uint8_t)((uint16_t)x << n))
            return 0;
    }
    x = 200;
    n = 5;
    return (uint16_t)(x <<= n) == 0;
}

int main(void)
{
    printf("from array %u\n", (unsigned)from_array());
    printf("from constant %u\n", (unsigned)from_constant());
    printf("after add %u\n", (unsigned)after_add());
    printf("local count %u\n", (unsigned)local_count());
    printf("edge counts %d\n", edge_counts());
    return 0;
}
