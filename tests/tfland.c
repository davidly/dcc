/* Regression for float locals across a short-circuiting nested loop. */
#include <stdint.h>
#include <stdio.h>

struct S0 {
    uint32_t m1;
    float m2[2];
    float m0;
    float m3;
};

float fg0 = 2.1474836e+09f;
static float fa[8] = { 3738.1921f, 0.3f, (-2.5f), 0.06999959f, 0.1f, 88535.42f, (-20526.39f), 3.0f };

static void hex4(uint16_t v)
{
    int8_t k;
    for (k = 12; k >= 0; k -= 4)
        putchar("0123456789abcdef"[(v >> k) & 15]);
}

static void show(uint16_t id, uint32_t v)
{
    hex4(id);
    putchar(' ');
    hex4((uint16_t)(v >> 16));
    hex4((uint16_t)v);
    putchar('\n');
}

static uint32_t ck;

static void mix(uint32_t v)
{
    ck = ((ck << 5) | (ck >> 27)) ^ v;
    ck = ck + (uint32_t)0x9e3779b1;
}


static float fclamp(float x, float b) { return x > b ? b : (x < -b ? -b : x); }
/* a float is shown by its IEEE bits; -0 is shown as 0 */
static void showf(uint16_t id, float f)
{
    union { float f; uint32_t u; } u;
    u.f = (f == 0.0f) ? 0.0f : f;
    show(id, u.u);
}

static void mixf(float f)
{
    union { float f; uint32_t u; } u;
    u.f = (f == 0.0f) ? 0.0f : f;
    mix(u.u);
}

static uint32_t state(void)
{
    uint8_t i, j;
    ck = 0;
    mixf(fg0);
    for (i = 0; i < 8; i++)
        mixf(fa[i]);
    return ck;
}


static uint16_t f5(void)
{
    float l0 = 0.2f;
    int16_t n3 = 3;
    int16_t n5 = 2;
    float w6 = 2028.3848f;
    while (n3 > 0) {
        while ((n5 > 0) && (w6 != (-0.154f))) {
            fa[1] = l0;
            l0 = 1.0f;
            n5 = 0;
        }
        l0 = 1.0f;
        n3 = 0;
    }
    fg0 = l0;
    return (uint16_t)1;
}

int main(void)
{
    show(1, (uint32_t)f5());
    show(16385, state());
    return 0;
}
