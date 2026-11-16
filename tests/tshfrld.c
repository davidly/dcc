/* tshfrld.c - a stored word reloaded after a shift of HL
 *
 * `x = (a >> 3) / (a / 7)` with `a` just stored: the peephole optimizer
 * once removed the reload of `a` for `a / 7` because the shift between
 * the store and the reload (`sra h` / `rr l`) was not seen to change HL,
 * so it divided the shifted value instead.
 */

#include <stdio.h>

static int fails;
static unsigned g;

static void check(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %lx want %lx\n", name, got, want);
        ++fails;
    }
}

static unsigned shift_right_div(unsigned a1, int a)
{
    g = a1;
    a = g;
    g = (a >> 3) / (a / 7);
    return g;
}

static unsigned shift_left_div(unsigned a1, int a)
{
    g = a1;
    a = g;
    g = (a << 2) / (a / 7);
    return g;
}

static unsigned shift_right_unsigned(unsigned a1, unsigned a)
{
    g = a1;
    a = g;
    g = (a >> 1) % (a / 3);
    return g;
}

int main(void)
{
    check("shift_right_div", shift_right_div(0x8000u, 0), 0L);
    check("shift_right_div2", shift_right_div(1400u, 0), 0L);
    check("shift_left_div", shift_left_div(700u, 0), 28L);
    check("shift_right_unsigned", shift_right_unsigned(0x9000u, 0), 0x4800L % 0x3000L);
    if (fails == 0)
        printf("tshfrld passed\n");
    return fails;
}
