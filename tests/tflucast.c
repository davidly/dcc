/* Unsigned long cast signedness when converting signed values to float:
 * implicit/explicit casts, arithmetic, and arguments, with stored-unsigned
 * and narrow-integer controls. */
#include <stdio.h>
#include <stdint.h>

static int32_t g2 = -72;
static int16_t gs = -72;
static float m;

static unsigned long bits(float f)
{
    union { float f; uint32_t u; } u;
    u.f = f;
    return u.u;
}

int main(void)
{
    uint32_t u;
    m = (uint32_t)g2;                /* 4294967224: 4f800000 */
    printf("%08lx\n", bits(m));
    m = (float)(uint32_t)g2;
    printf("%08lx\n", bits(m));
    m = (uint32_t)(g2 + 1);
    printf("%08lx\n", bits(m));
    m = (uint32_t)g2 * 1.0f;
    printf("%08lx\n", bits(m));
    printf("%08lx\n", bits((uint32_t)(int32_t)gs));
    /* accepted forms */
    u = (uint32_t)g2;
    m = u;
    printf("%08lx %08lx %08lx\n", bits(m), bits((uint32_t)gs), bits((uint16_t)gs));  /* 4f800000 4f800000 477fb800 */
    m = 0.0f + (uint32_t)g2;
    printf("%08lx\n", bits(m));
    return 0;
}
