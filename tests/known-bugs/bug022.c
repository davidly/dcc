/* BUG-022: when a label name used in one function is reused inside a nested
 * block of a later function, dcc stops reporting "MIR emission is required"
 * for functions after that. It exits 0 and the program hangs or restarts in
 * a loop when the function it could not emit is called. With the second
 * label renamed, or not inside a nested block, the compile fails as it
 * should (see BUG-008 and BUG-019 for the function it cannot emit). */
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
