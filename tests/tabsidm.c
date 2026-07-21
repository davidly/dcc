#include <stdio.h>

/* Regression for the abs-idiom fast path (`x < 0 ? -x : x` and the mirror
 * `x >= 0 ? x : -x`). It may fire ONLY when the comparison against zero is
 * genuinely signed:
 *
 *   - a signed operand compared with a signed 0  -> real abs (fires);
 *   - a signed operand compared with an UNSIGNED zero literal (0U/0UL) ->
 *     the comparison is done unsigned, so `x < 0U` is constant-false and
 *     the value is x unchanged (must NOT negate: e.g. -12345 stays -12345);
 *   - an unsigned operand -> value is x unchanged (was miscompiled: the old
 *     path negated whenever bit 7 of the high byte was set, e.g. 40000 to
 *     25536).
 *
 * Operands are derived from argc (== 1 at runtime, opaque to the constant
 * folder) rather than `volatile` locals, so the codegen path is exercised
 * without also asserting the invalid contraction of volatile accesses - a
 * volatile object must be re-read on every access, which this single-load
 * idiom deliberately does not do. */

int main(int argc, char **argv)
{
    int si = -12345 * argc;
    int sp = 6789 * argc;
    unsigned int ui = 40000u * (unsigned int)argc;   /* >= 0x8000 */
    unsigned int uz = 25u * (unsigned int)argc;
    unsigned short us = (unsigned short)(50000u * (unsigned int)argc); /* >= 0x8000 */
    signed char sc = (signed char)(-42 * argc);
    unsigned char uc = (unsigned char)(200 * argc);  /* promotes to signed int */

    int a = (si < 0) ? -si : si;            /* 12345  (real signed abs) */
    int b = (sp < 0) ? -sp : sp;            /* 6789 */
    int c = (si >= 0) ? si : -si;           /* 12345 */
    int d = (si < 0U) ? -si : si;           /* -12345 (unsigned zero: value is si) */
    int e = (si >= 0U) ? si : -si;          /* -12345 */
    int f = (si < 0UL) ? -si : si;          /* -12345 */
    unsigned int g = (ui < 0) ? -ui : ui;   /* 40000 (unsigned operand: value is ui) */
    unsigned int h = (uz >= 0) ? uz : -uz;  /* 25 */
    unsigned int i = (us < 0) ? (unsigned short)-us : us; /* 50000 */
    int j = (sc < 0) ? -sc : sc;            /* 42 */
    int k = (uc < 0) ? -uc : uc;            /* 200 */

    (void)argv;
    printf("tabsidm a=%d b=%d c=%d d=%d e=%d f=%d g=%u h=%u i=%u j=%d k=%d\n",
           a, b, c, d, e, f, g, h, i, j, k);
    return 0;
}
