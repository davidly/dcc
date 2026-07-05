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

    /* Added for the fully register-resident __fadd/__fsub rewrite (both
     * operands' entire unpacked state, not just the alignment/renormalize
     * shifts, live in registers across the main+alternate Z80 banks).
     * These specifically target the cross-bank subtract-direction logic
     * (FSUBTR/FBBIG): differing exponents combined with either operand
     * being the bigger magnitude, via both __fadd (opposite signs) and
     * __fsub (same signs, flipped internally) entry points, plus heavy
     * cancellation forcing many renormalize shifts and -0.0 handling. */

    a = 5.0f; b = 3.0f; r = a - b;
    printf("%f\n", r);                 /* 2.000000 - A bigger, no sign flip */

    a = 10.0f; b = -3.0f; r = a + b;
    printf("%f\n", r);                 /* 7.000000 - add, opposite signs,
                                           A bigger magnitude and exponent */

    a = 3.0f; b = -10.0f; r = a + b;
    printf("%f\n", r);                 /* -7.000000 - add, opposite signs,
                                           B bigger magnitude and exponent:
                                           exercises the cross-bank negate
                                           (FBBIG) via __fadd directly, not
                                           just __fsub's internal flip */

    a = -3.0f; b = 10.0f; r = a + b;
    printf("%f\n", r);                 /* 7.000000 - mirror of the above */

    a = 1000.0f; b = -0.001f; r = a + b;
    printf("%f\n", r);                 /* 999.999023 - NOT negligible: the
                                           exponent gap here is ~19, under
                                           the diff>=24 threshold, so B's
                                           truncated-but-nonzero residual
                                           still perturbs the result;
                                           verified byte-for-byte identical
                                           to the pre-rewrite implementation
                                           via differential testing */

    a = 0.001f; b = -1000.0f; r = a + b;
    printf("%f\n", r);                 /* -999.999023 - mirror of the above,
                                           B's sign wins (B is bigger) */

    a = 1048576.0f; b = -1048577.0f; r = a + b;
    printf("%f\n", r);                 /* -1.000000 - heavy cancellation
                                           (many renormalize left-shifts)
                                           combined with B being bigger */

    a = 0.0f; b = -0.0f; r = a + b;
    printf("%f\n", r);                 /* 0.000000 - zero shortcut path
                                           with a negative-zero operand */

    a = -0.0f; b = 0.0f; r = a + b;
    printf("%f\n", r);                 /* 0.000000 - mirror */

    return 0;
}
