/*
 * tpfinf.c - printf %f formatting of +-Inf and +-NaN.
 *
 * Host validation of this test is skipped on MSVC and macOS (see
 * tests/_test_overrides.json's requires-non-msvc-host-compiler and
 * requires-non-macos-host-compiler): negating NAN flips its sign bit, and
 * what printf does with that sign bit on a NaN is implementation-defined -
 * the C standard doesn't specify it. MSVC's UCRT printf specifically
 * recognizes the resulting x87 "indefinite" QNaN bit pattern (0xffc00000
 * for float) and renders it "nan(ind)" instead of plain "nan". Apple's libc
 * printf drops the sign entirely, printing "nan" for both +NAN and -NAN.
 * glibc (and dcc's own target printf) both format a negated NaN as "-nan",
 * which is what this test's baseline expects - neither of the other two is
 * a dcc bug, just a different (and equally valid) libc convention.
 */
#include <stdio.h>
#include <math.h>

int main(void)
{
    float pinf = INFINITY;
    float ninf = -INFINITY;
    float nan = NAN;
    float nnan = -NAN;

    printf("[%f]\n", pinf);
    printf("[%f]\n", ninf);
    printf("[%f]\n", nan);
    printf("[%f]\n", nnan);
    printf("[%10f]\n", pinf);
    printf("[%-10f]\n", pinf);
    printf("[%010f]\n", ninf);
    printf("[%.2f]\n", pinf);
    printf("[%.0f]\n", nan);
    return 0;
}
