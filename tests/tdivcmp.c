/* tdivcmp.c - int compared with a long quotient or remainder
 *
 * `x < n / x` with int x and long n failed with "MIR emission is required":
 * the spilled emitter paired the converted x as the right operand of a
 * 32-bit helper whose left operand (n, or a constant) it expected already
 * pushed, but a parameter or constant is materialized where it is used.
 */

#include <stdio.h>

static int fails;

static void check(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %lx want %lx\n", name, got, want);
        ++fails;
    }
}

static int if_lt_quotient(int x, long n) { if (x < n / x) return 1; return 0; }
static int lt_quotient(int x, long n) { return x < n / x; }
static long lt_quotient_long(int x, long n) { return x < n / x; }
static int ge_remainder(int x, long n) { return x >= n % x; }
static short lt_const_quotient(int x) { return (short)((x + x) < (65536L / x)); }

int main(void)
{
    check("if_lt_quotient", if_lt_quotient(3, 100L), 1L);
    check("if_lt_quotient_0", if_lt_quotient(20, 100L), 0L);
    check("lt_quotient", lt_quotient(300, 100000L), 1L);
    check("lt_quotient_0", lt_quotient(400, 100000L), 0L);
    check("lt_quotient_long", lt_quotient_long(3, 100L), 1L);
    check("ge_remainder", ge_remainder(7, 100L), 1L);
    check("ge_remainder_0", ge_remainder(7, 6L), 1L);
    check("lt_const_quotient", lt_const_quotient(100), 1L);
    check("lt_const_quotient_0", lt_const_quotient(300), 0L);
    if (fails == 0)
        printf("tdivcmp passed\n");
    return fails;
}
