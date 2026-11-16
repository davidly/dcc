/* Guarded constant-zero division and remainder retain runtime operations,
 * including an int constant divided by a long zero (`3 / 0L`), which was
 * once rejected as unsupported. */
#include <stdint.h>
#include <stdio.h>

static int16_t never = 0;
static int16_t out;

static int16_t in_return(void)
{
    if (never)
        return 1000 / 0;
    return 1;
}

static void in_assignment(void)
{
    if (never)
        out = 7 % 0;
    else
        out = 2;
}

static int32_t wide;
static uint8_t byte;

static void mixed_long(void)
{
    wide = 7;
    byte = 9;
    if (never) {
        out = 3 / 0L;
        wide = (-19935) % 0L;
        byte *= (-19935) % 0L;
    }
}

int main(void)
{
    in_assignment();
    printf("%d %d\n", in_return(), out);
    mixed_long();
    printf("%ld %d %ld\n", (long)wide, byte, 30 / 7L + 30 % 7L);
    return 0;
}
