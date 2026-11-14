/* Float constant unary, logical, truth, signed-zero and short-circuit tests. */
#include <stdio.h>

static float f, g;
static int n;

int main(void)
{
    f = 2.0f;
    g = -(-2.5f);                       /* 2.5 */
    printf("%d\n", (int)(g * 10.0f));
    g = -(1.0f + 2.0f);                 /* -3 */
    printf("%d\n", (int)g);
    n = !255.0f;                        /* 0 */
    printf("%d\n", n);
    n = !(1.0f + 2.0f);                 /* 0 */
    printf("%d\n", n);
    n = 2.5f && 1;                      /* 1 */
    printf("%d\n", n);
    if (2.3f) n = 7;                    /* taken */
    printf("%d\n", n);
    if (0.0f) n = 8;                    /* not taken */
    printf("%d\n", n);
    if (1.0f + 2.0f) n = 9;             /* taken */
    printf("%d\n", n);
    while (1.0f) { n++; break; }        /* 10 */
    printf("%d\n", n);
    /* accepted */
    g = -(2.5f) + (f ? 1.0f : 0.0f) + (2.5f ? 1.0f : 0.0f);
    if (f) n = !f;
    printf("%d %d\n", (int)(g * 10.0f), n);

    n = !0.25f;
    printf("%d\n", n);
    n = !(-0.0f);
    printf("%d\n", n);
    n = !!0.25f;
    printf("%d\n", n);
    n = ~(!0.25f);
    printf("%d\n", n);
    n = (1.0f < 2.0f) && (0.0f == 0.0f);
    printf("%d\n", n);
    n = (1.0f > 2.0f) || (1 == 1);
    printf("%d\n", n);
    if ((1.0f > 2.0f) && (0.0f == 0.0f)) n = 8;
    printf("%d\n", n);
    n = 0;
    if (0.0f && (n = 1)) n = 9;
    if (1.0f || (n = 1)) n += 2;
    printf("%d\n", n);
    g = -(-(-2.5f));
    printf("%d\n", (int)(g * 10.0f));
    return 0;
}
