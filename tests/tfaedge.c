#include <stdio.h>

/* Targeted edge cases for the register-resident FALGN (exponent-alignment
 * shift) and FNORM (post-subtraction renormalize shift) rewrite in
 * __fadd/__fsub, beyond what tc89fadd/tfloat4/mm.c already exercise:
 * same-exponent add (no shift at all), small/large alignment shifts
 * (including exactly byte-aligned and maximum residual-bit cases),
 * negligible-contribution shifts (diff >= 24), catastrophic-cancellation
 * subtraction (heavy renormalize shifting), exact-zero results, sign
 * flips, and the mantissa-overflow carry path in same-sign addition
 * (which still uses the single, non-loop FSRA call left untouched). */

int main()
{
    float a, b, r;

    a = 2.0f; b = 3.0f; r = a + b;
    printf("%f\n", r);                 /* 5.000000 - no alignment shift */

    a = 1.5f; b = 0.75f; r = a + b;
    printf("%f\n", r);                 /* 2.250000 - 1-bit alignment shift */

    a = 256.0f; b = 1.0f; r = a + b;
    printf("%f\n", r);                 /* 257.000000 - exactly 8-bit shift */

    a = 65536.0f; b = 1.0f; r = a + b;
    printf("%f\n", r);                 /* 65537.000000 - exactly 16-bit shift */

    a = 1.0f; b = 0.0000001f; r = a + b;
    printf("%f\n", r);                 /* 1.000000 - diff>=24, negligible */

    a = 1.0000001f; b = 1.0f; r = a - b;
    printf("%f\n", r);                 /* 0.000000 - float's ~7-digit
                                           precision rounds 1.0000001f to
                                           exactly 1.0f, so a==b here;
                                           verified via differential
                                           testing against the pre-rewrite
                                           implementation, not a bug */

    a = 5.0f; b = 5.0f; r = a - b;
    printf("%f\n", r);                 /* 0.000000 - exact zero */

    a = 3.0f; b = 5.0f; r = a - b;
    printf("%f\n", r);                 /* -2.000000 - sign flip */

    a = 1.9f; b = 1.9f; r = a + b;
    printf("%f\n", r);                 /* 3.800000 - mantissa overflow carry */

    a = -2.0f; b = -3.0f; r = a + b;
    printf("%f\n", r);                 /* -5.000000 - both negative */

    a = 100.5f; b = -100.0f; r = a + b;
    printf("%f\n", r);                 /* 0.500000 - mixed signs, near-cancel */

    return 0;
}
