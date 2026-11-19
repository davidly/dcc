/* Regression for float compound division with a deferred member-array load. */
#include <stdint.h>
#include <stdio.h>

struct S {
    float m3;
    float m2[2];
};

static struct S s0 = { 0.0f, { 0.75f, -261.66437f } };
static struct S s1[2] = { { 0.0f, { 0.0f, 0.0f } },
                          { 1.5f, { 0.0f, 0.0f } } };
static float known = 6.0f;

static void showf(uint16_t id, float value)
{
    union { float f; uint32_t u; } bits;
    int shift;
    bits.f = value == 0.0f ? 0.0f : value;
    for (shift = 12; shift >= 0; shift -= 4)
        putchar("0123456789abcdef"[(id >> shift) & 15]);
    putchar(' ');
    for (shift = 28; shift >= 0; shift -= 4)
        putchar("0123456789abcdef"[(bits.u >> shift) & 15]);
    putchar('\n');
}

static void update(void)
{
    s1[1].m3 /= (-(--s0.m2[0]));
}

static void update_known(void)
{
    known /= 3.0f;
}

int main(void)
{
    update();
    showf(1, s1[1].m3);
    showf(2, s0.m2[0]);
    update_known();
    showf(3, known);
    return 0;
}
