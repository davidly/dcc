/* BUG-040: the initializer of a float with static storage must be a bare
 * floating constant, optionally negated, or `(float)` of an integer. A
 * parenthesised constant (`(2.5f)`), a negated parenthesised constant and a
 * constant expression (`1.0f + 2.0f`) are rejected with DCC-E1102 "expected
 * ';'" (file scope) or DCC-E0912 "float initializer must be constant" (an
 * array element). Integer initializers and automatic float initializers
 * accept all of these. */
#include <stdio.h>

static float a = (2.5f);
static float b = -(2.5f);
static float c = 1.0f + 2.0f;
static float d = 3 * 0.5f;
static float arr[3] = { (0.5f), -(0.25f), 1.0f - 0.75f };
static int ok = (-2) + 1 * 3;

int main(void)
{
    float la = (2.5f), lb = -(2.5f), lc = 1.0f + 2.0f;
    printf("%d %d %d %d\n", (int)(a * 10.0f), (int)(b * 10.0f), (int)c, (int)(d * 10.0f));
    printf("%d %d %d %d\n", (int)(arr[0] * 100.0f), (int)(arr[1] * 100.0f), (int)(arr[2] * 100.0f), ok);
    printf("%d %d %d\n", (int)(la * 10.0f), (int)(lb * 10.0f), (int)lc);
    return 0;
}
