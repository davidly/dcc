/* BUG-041: the value of a float `++` or `--` expression is rejected with
 * DCC-E1002 "unsupported expression statement" / "unsupported if condition
 * or branch" / "unsupported while condition or body" wherever it is used:
 * assigned, compared, tested for truth, in `?:`, on a global, an array
 * element or a local. `f++;` as a statement and the value of `f += 1.0f`
 * are accepted, and so is the value of an integer `++`. */
#include <stdio.h>

static float f, g, fa[4];
static int n;

int main(void)
{
    float l = 2.0f;

    f = 2.0f;
    g = f++;                               /* 2, f 3 */
    printf("%d %d\n", (int)g, (int)f);
    g = ++f + 1.0f;                        /* 5, f 4 */
    printf("%d %d\n", (int)g, (int)f);
    if (f-- > 3.0f) g = 7.0f;              /* taken, f 3 */
    printf("%d %d\n", (int)g, (int)f);
    if (--f) n = 1;                        /* 2 is true */
    printf("%d %d\n", n, (int)f);
    n = f++ > 1.0f;                        /* 1, f 3 */
    printf("%d %d\n", n, (int)f);
    g = (f-- > 10.0f) ? 1.0f : 2.0f;       /* 2, f 2 */
    printf("%d %d\n", (int)g, (int)f);
    while (f-- > 0.0f) g += 1.0f;          /* two passes: g 4, f -1 */
    printf("%d %d\n", (int)g, (int)f);
    fa[1] = 1.5f;
    if (fa[1]++ > 1.0f) g = 9.0f;          /* taken, fa[1] 2.5 */
    printf("%d %d\n", (int)g, (int)(fa[1] * 2.0f));
    if (l++ > 1.0f) g = 11.0f;             /* taken, l 3 */
    printf("%d %d\n", (int)g, (int)l);
    g = (l += 1.0f);                       /* accepted: 4 */
    printf("%d\n", (int)g);
    return 0;
}
