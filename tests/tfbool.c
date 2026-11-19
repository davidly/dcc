/* Regression for converting a constant logical expression to float. */
#include <stdint.h>
#include <stdio.h>

struct S0 {
    float m3;
    float m2[2];
    float m0;
    int16_t m1;
};

static float fg1 = 66.67611f;

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
    mixf(fg1);
    return ck;
}


static float f2(void)
{
    fg1 = (1 || 0.0f);
    return (uint16_t)1;
}

int main(void)
{
    showf(1, f2());
    show(16385, state());
    fg1 = (0 && 0.0f);
    showf(2, fg1);
    fg1 = (0 || 0.0f);
    showf(3, fg1);
    fg1 = (1 && 1.0f);
    showf(4, fg1);
    fg1 = (0 || 1.0f);
    showf(5, fg1);
    return 0;
}
