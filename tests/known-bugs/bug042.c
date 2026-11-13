/* BUG-042: a floating constant that has to be folded or tested for truth
 * is rejected with DCC-E1002: the operand of unary `-` when it is itself a
 * negated or computed constant, the operand of `!`, `&&` or `||`, and the
 * condition of `if` or `while`. `-(2.5f)`, `2.5f ? a : b` and the same
 * shapes with a variable or with integer constants are accepted. */
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
    return 0;
}
