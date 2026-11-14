/* Floating logical negation returns true for both signs of zero.
 * Cover zeros produced by conversion/negation, a literal and multiplication,
 * plus the neighboring branch, ternary, logical and equality truth tests. */
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
