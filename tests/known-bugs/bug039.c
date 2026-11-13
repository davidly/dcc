/* BUG-039: float division is off by one unit in the last place in about one
 * fifth of cases, in both directions. Addition, subtraction, multiplication
 * and every conversion are bit-exact. The operands are volatile so that the
 * host compiler cannot fold the division; dcc never folds a float division
 * and calls the runtime's __fdiv for every quotient. */
#include <stdint.h>
#include <stdio.h>

typedef union { float f; uint32_t u; } bits;

static const float vals[] = {
    1.0f, 3.0f, 0.1f, 0.2f, 1.9f, 3.14159f, 2.71828f, 123456.0f, 0.000123f,
    -7.5f, 1e-5f, 65535.0f, 32767.0f, 1e10f, 1e-10f, 16777216.0f, 0.3f, 0.7f,
    123.456f, 9.99999f
};
#define N (sizeof(vals) / sizeof(vals[0]))

volatile float va, vb;

static void show(const char *tag, float f)
{
    bits b;
    b.f = f;
    printf("%s %08lx\n", tag, (unsigned long)b.u);
}

int main(void)
{
    unsigned i, j;
    char tag[16];

    /* well-known cases: want 3eaaaaab 3ec4ec4e 3f0a60dd 40555555 */
    va = 1.0f; vb = 3.0f; show("1/3", va / vb);
    va = 3.0f; vb = 7.8f; show("3/7.8", va / vb);
    va = 1.0f; vb = 1.85f; show("1/1.85", va / vb);
    va = 10.0f; vb = 3.0f; show("10/3", va / vb);

    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            va = vals[i];
            vb = vals[j];
            sprintf(tag, "%u/%u", i, j);
            show(tag, va / vb);
        }
    }
    return 0;
}
