/* Static float constant arithmetic, precedence, casts, and IEEE rounding. */
#include <stdio.h>
#include <stdint.h>

static float a = (2.5f);
static float b = -(2.5f);
static float c = 1.0f + 2.0f;
static float d = 3 * 0.5f;
static float arr[3] = { (0.5f), -(0.25f), 1.0f - 0.75f };
static int ok = (-2) + 1 * 3;

static float values[] = {
    (2.5f), -(2.5f), 1.0f + 2.0f * 3.0f, (1.0f + 2.0f) * 3.0f,
    3 * (0.5f + 0.25f) / 2 - 0.125f,
    (16777216.0f + 1.0f) - 16777216.0f,
    (1 / 2) + 0.25f, ((float)(3 + 2)) * 0.5f
};

int main(void)
{
    int i;
    union { float f; uint32_t u; } bits;
    float la = (2.5f), lb = -(2.5f), lc = 1.0f + 2.0f;
    printf("%d %d %d %d\n", (int)(a * 10.0f), (int)(b * 10.0f), (int)c, (int)(d * 10.0f));
    printf("%d %d %d %d\n", (int)(arr[0] * 100.0f), (int)(arr[1] * 100.0f), (int)(arr[2] * 100.0f), ok);
    printf("%d %d %d\n", (int)(la * 10.0f), (int)(lb * 10.0f), (int)lc);
    for (i = 0; i < 8; ++i) {
        bits.f = values[i];
        printf("%lu\n", (unsigned long)bits.u);
    }
    return 0;
}
