/* BUG-050: `(int32_t)(uint32_t)x`, with x a signed 8- or 16-bit value,
 * converted to float gives the unsigned value: -121 becomes
 * 4294967175.0f instead of -121.0f. With a 32-bit x, or without the
 * inner cast, the conversion is right. Mirror image of BUG-048. */
#include <stdio.h>
#include <stdint.h>

static int8_t g4 = -121;
static int16_t g5 = -121;
static int32_t g6 = -121;
static float fg;

static unsigned long bits(float f)
{
    union { float f; uint32_t u; } u;
    u.f = f;
    return u.u;
}

int main(void)
{
    fg = (int32_t)((uint32_t)g4);
    printf("%08lx\n", bits(fg));          /* -121: c2f20000 */
    fg = (int32_t)((uint32_t)g5);
    printf("%08lx\n", bits(fg));          /* c2f20000 */
    fg = (int32_t)((uint32_t)g4 + 1);
    printf("%08lx\n", bits(fg));          /* -120: c2f00000 */
    /* right */
    fg = (int32_t)((uint32_t)g6);
    printf("%08lx %08lx %08lx\n", bits(fg), bits((int32_t)g4), bits((uint32_t)g4));  /* c2f20000 c2f20000 4f800000 */
    return 0;
}
