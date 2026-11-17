/* Function-scoped label reuse inside nested blocks, followed by nested
 * declarations, wide-to-narrow casts and dead declarations after return.
 * Earlier functions' label metadata must not contaminate later functions. */
#include <stdint.h>
#include <stdio.h>

static uint16_t low;
static int16_t sel = 1;

static uint16_t first(void)
{
L1:;
    return 1;
}

static uint16_t second(void)
{
    {
L1:;
    }
    return 2;
}

static int8_t third(void)
{
    {
        int32_t b = 0;
        b = -8305;
        low = (uint16_t)b;
    }
    return 3;
}

static int8_t fourth(void)
{
    if (sel) {
        return 4;
        {
            int16_t b = 1;
            low += (uint16_t)b;
        }
    }
    return 0;
}

int main(void)
{
    int8_t result;
    printf("first %u\n", (unsigned)first());
    printf("second %u\n", (unsigned)second());
    result = third();
    printf("third %d %u\n", (int)result, (unsigned)low);
    result = fourth();
    printf("fourth %d %u\n", (int)result, (unsigned)low);
    return 0;
}
