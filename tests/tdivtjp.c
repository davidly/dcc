/* tdivtjp.c - a constant division ending a function
 *
 * `return *p / 32767u;` becomes a call of the constant-divisor helper that
 * the peephole optimizer then turns into a tail jump. The pass that
 * re-declares the helpers still in use counted only calls, so the jump's
 * helper lost its EXTRN and the module did not assemble.
 */

#include <stdio.h>

static int fails;
static unsigned g = 65535u;
static unsigned h = 32766u;

static void check(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %lx want %lx\n", name, got, want);
        ++fails;
    }
}

unsigned quotient(unsigned *p) { return *p / 32767u; }
unsigned remainder(unsigned *p) { return *p % 32767u; }

int main(void)
{
    check("quotient", quotient(&g), 2L);
    check("quotient_small", quotient(&h), 0L);
    check("remainder", remainder(&g), 1L);
    if (fails == 0)
        printf("tdivtjp passed\n");
    return fails;
}
