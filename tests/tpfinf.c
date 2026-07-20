/*
 * tpfinf.c - printf %f formatting of +-Inf and +-NaN.
 *
 * Host validation of this test is skipped on MSVC (see
 * tests/_test_overrides.json's requires-non-msvc-host-compiler): negating
 * NAN flips its sign bit to the classic x87 "indefinite" QNaN pattern
 * (0xffc00000 for float), and MSVC's UCRT printf specifically recognizes
 * that one bit pattern and renders it "nan(ind)" instead of plain "nan" -
 * a real, documented MSVC CRT formatting convention, not a dcc bug. glibc
 * (and dcc's own target printf) format all NaNs uniformly.
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
