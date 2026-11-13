/* BUG-045: a comparison whose right operand is a negative float constant
 * combined with a second operand (`x < -1.0f - n`, `x == -1.0f * n`) stops
 * the compile with `dcc: fatal: MIR emission is required`. The same
 * expression assigned to a float first, or written with the constant on the
 * right (`x < n - 1.0f`), or with a positive constant, compiles. */
#include <stdio.h>
#include <stdint.h>

static float fg = -5.0f;
static int n = 3;
static int32_t l = 3;

static int f(float x)
{
    int r = 0;
    if (x < -1.0f - n) r |= 1;          /* -5 < -4: taken */
    if (x < -1.0f + n) r |= 2;          /* -5 < 2: taken */
    if (x == -1.0f * 5) r |= 4;         /* taken */
    if (x < -1.0f - l) r |= 8;          /* taken */
    if (x < -1.0f - 2) r |= 16;         /* taken */
    if (x < -1.0f - fg) r |= 32;        /* -5 < 4: taken */
    while (x < -1.0f / n) { r |= 64; break; }
    r |= (x < -0.0f - n) ? 128 : 0;
    return r;
}

int main(void)
{
    float t;
    printf("%d\n", f(fg));
    /* accepted forms */
    t = -1.0f - n;
    printf("%d %d %d\n", fg < t, fg < n - 1.0f, fg < 1.0f - n);
    return 0;
}
