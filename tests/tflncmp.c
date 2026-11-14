/* Float comparisons against negative and signed-zero constants combined
 * with integer/float operands, all relations, assignment/loop/conditional
 * forms, unary spelling variants and accepted neighboring expressions. */
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

static int negforms(float x, int16_t y) {
    return (x <= -(1.0f) - y) | ((x > (-1.0f) + y) << 1)
         | ((x >= -1.0f * y) << 2) | ((x != -0.0f - y) << 3);
}

static int constant_left(float x)
{
    return (1.0f <= (3.0f * fg)) | ((2 < (3.0f * fg)) << 1)
         | ((x < (1174.3f + 1)) << 2);
}

int main(void)
{
    float t;
    int c;
    printf("%d\n", f(fg));
    /* accepted forms */
    t = -1.0f - n;
    printf("%d %d %d\n", fg < t, fg < n - 1.0f, fg < 1.0f - n);
    printf("negative %d %d\n", negforms(-5.0f, 3), negforms(5.0f, 3));
    c = constant_left(-5.0f);
    fg = 5.0f;
    printf("constant left %d %d\n", c, constant_left(2000.0f));
    return 0;
}
