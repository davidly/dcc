/* Integer compound updates from deferred float expressions: seed 17844
 * plus prefix/postfix member values, all integer widths and controls. */
#include <stdint.h>
#include <stdio.h>

struct S0 {
    float m3;
    float m0;
    int32_t m1;
    float m2[2];
};

static int16_t g0 = 1000;

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
    mix((uint32_t)g0);
    return ck;
}


static float f2(void)
{
    {
        float b4 = -1.5868155f;
        g0 *= (b4 = 7.0f);
    }
    return (int32_t)1;
}

static int8_t c8 = -66;
static uint8_t u8 = 20;
static int32_t c32 = 100;
static uint32_t u32 = 100;
static uint16_t u16 = 100;
static struct { float pad; float a[2]; } items[2];
static void controls(void)
{
    items[1].a[0] = 1.5f;
    items[1].a[1] = 2.0f;
    c8 -= (--items[1].a[0]);
    u8 += (items[1].a[1]++);
    c32 *= (--items[1].a[1]);
    u32 -= (++items[1].a[0]);
    u16 += (items[1].a[1]--);
    printf("members %d %u %ld %lu %u\n", c8, u8, (long)c32, (unsigned long)u32, u16);
    g0 = 100;
    {
        float local = 1.0f;
        g0 += local++;
    }
    printf("local %d\n", g0);
    g0 = 100;
    g0 *= 2.0f;
    printf("constant %d\n", g0);
}

int main(void)
{
    showf(1, f2());
    show(16385, state());
    controls();
    return 0;
}
