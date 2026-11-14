/* Signed long cast signedness before conversion to float: nested casts
 * from narrow integers, folded/arithmetic expressions, and unsigned
 * globals, locals, and parameters. Signed and unsigned controls remain. */
#include <stdio.h>
#include <stdint.h>

static int8_t g4 = -121;
static int16_t g5 = -121;
static int32_t g6 = -121;
static float fg;
static uint32_t gu = 0xffffffbdUL;
static int32_t gl = 7;
static float from_unsigned(uint32_t x) { return (float)(int32_t)x; }

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
    fg = (float)(int32_t)(2147483647L + 1UL);
    printf("%08lx\n", bits(fg));
    fg = (float)(int32_t)(gl + 2147483647UL);
    printf("%08lx\n", bits(fg));
    fg = (int32_t)gu;
    printf("%08lx %08lx\n", bits(fg), bits(from_unsigned(gu)));
    {
        uint32_t local = 0xffffffbdUL;
        fg = (float)(int32_t)local;
        printf("%08lx\n", bits(fg));
    }
    return 0;
}
