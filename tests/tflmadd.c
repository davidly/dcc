/* Float multiply-add operand preservation across fused MIR emission.
 * Covers integer conversions and global, local, parameter, array and
 * sub-expression factors, plus += and correctly ordered controls. */
#include <stdio.h>
#include <stdint.h>

static float fg = 1.0f, fv = 3.0f;
static int32_t gl = 2;
static uint16_t gu = 2;

static float fa[2] = {0.0f, 3.0f};

static float t(float p) { float l1 = 3.0f; return fg + (fv * p) + (fg + (fa[1] * l1)); }   /* 20 */

static unsigned long bits(float f)
{
    union { float f; uint32_t u; } u;
    u.f = f;
    return u.u;
}

int main(void)
{
    int i = 2;
    float r;
    printf("%08lx\n", bits(fg + (2 * 3.0f)));          /* 7:  40e00000 */
    printf("%08lx\n", bits(fg + (i * 3.0f)));          /* 7 */
    printf("%08lx\n", bits(fg + (gl * fv)));           /* 7 */
    printf("%08lx\n", bits(fg + gu * fv));             /* 7 */
    printf("%08lx\n", bits(1.0f + (128 * 1.0f)));      /* 129: 43010000 */
    printf("%08lx\n", bits(fg + (2 * 3.0f) + 1.0f));   /* 8 */
    r = fg;
    r += 2 * 3.0f;                                     /* 7 */
    printf("%08lx\n", bits(r));
    r = fg;
    r += (int32_t)1 * 1.0f;                            /* 2 */
    printf("%08lx\n", bits(r));
    {
        float l1 = 3.0f;
        printf("%08lx\n", bits(fg + (fv * l1)));             /* 10: 41200000 */
        printf("%08lx\n", bits(fg + (fv * l1 * l1)));        /* 28: 41e00000 */
        printf("%08lx\n", bits(fg + ((fv + 0.0f) * l1)));    /* 10 */
        printf("%08lx\n", bits(t(3.0f)));                    /* 20: 41a00000 */
        printf("%08lx\n", bits(fg + (l1 * fv)));             /* 10 (right) */
    }
    /* accepted forms */
    printf("%08lx %08lx %08lx %08lx\n", bits(fg - (2 * 3.0f)), bits((2 * 3.0f) + fg),
           bits(fg + fv * i), bits(fg + (float)i * 3.0f));   /* c0a00000 40e00000 40e00000 40e00000 */
    return 0;
}
