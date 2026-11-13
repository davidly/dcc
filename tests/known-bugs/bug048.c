/* BUG-048: converting `(uint32_t)x` to float, where x is a signed 32-bit
 * expression, converts the signed value instead: (float)(uint32_t)-72 is
 * -72.0f rather than 4294967224.0f. Storing the cast in a uint32_t variable
 * first, or casting a signed 8- or 16-bit value, converts correctly. */
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
    return 0;
}
