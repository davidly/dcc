/* BUG-049: `!f` is 0 when the float f is negative zero; it should be 1.
 * The truth test in `if (f)`, `f ? :`, `&&`, `||` and the comparison
 * `f == 0.0f` all treat -0.0f as zero correctly; only `!` looks at the
 * raw bits. -0.0f arises from `-(float)0`, `0.0f * -1.0f` and similar. */
#include <stdio.h>
#include <stdint.h>

static float m0, one = 1.0f, zero = 0.0f, minus = -1.0f;
static uint8_t g5 = 0;

int main(void)
{
    m0 = -((float)g5);
    printf("%d\n", !m0);                 /* 1 */
    m0 = -0.0f;
    printf("%d\n", !m0);                 /* 1 */
    printf("%d\n", !(zero * minus));     /* 1 */
    if (!m0) printf("yes\n"); else printf("no\n");   /* yes */
    /* correct */
    printf("%d %d %d %d %d\n", m0 == 0.0f, m0 ? 1 : 0, m0 && 1, m0 || 0, !one);  /* 1 0 0 0 0 */
    return 0;
}
