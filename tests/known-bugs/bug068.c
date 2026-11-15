/* BUG-068: compound assignment through a nested-block pointer to a 32-bit
 * object adds only the low word of the right-hand side.
 *
 * T2 (BUG-063) repaired the plain store `*q = x` through a pointer declared in
 * a nested block; the compound forms `*q += x`, `*q -= x` (and the rest)
 * still keep the 16-bit conversion inserted for the assumed target on the
 * right-hand operand, so `15 += 0x00ff00ff` yields 15 + 0x00ff = 270.
 * The same statements with `q` declared at function scope are correct.
 *
 * Expected (clang): 16711950 16712206 65599 -16712063 / 16711950
 * dcc main 7dbe99d8: 270 526 63 -383 / 16711950
 */
#include <stdio.h>
#include <stdint.h>

int32_t a3[4] = { 15, 271, 63, -128 };
static struct { int32_t m0; } s0 = { 16711935 };
static int32_t g = 16711935;

static void function_scope(void)
{
    int32_t cell = 15;
    int32_t *q = &cell;
    *q += s0.m0;
    printf("%ld\n", (long)cell);
}

int main(void)
{
    { int32_t *q = &a3[0]; (*q) += s0.m0; }
    { int32_t *q = &a3[1]; (*q) += g; }
    { int32_t *q = &a3[2]; (*q) += (int32_t)65536; }
    { int32_t *q = &a3[3]; (*q) -= s0.m0; }
    printf("%ld %ld %ld %ld\n", (long)a3[0], (long)a3[1], (long)a3[2], (long)a3[3]);
    function_scope();
    return 0;
}
