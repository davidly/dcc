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
    printf("first %u\n", (unsigned)first());
    printf("second %u\n", (unsigned)second());
    printf("third %d %u\n", (int)third(), (unsigned)low);
    printf("fourth %d %u\n", (int)fourth(), (unsigned)low);
    return 0;
}
