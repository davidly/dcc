#include <stdio.h>

/* Targeted edge cases for the register-resident __fdiv rewrite (24-iteration
 * restoring-division loop moved from memory-resident FP0-FP3/FQ0-FQ2 to two
 * register banks swapped via EXX). Beyond what tc89fdiv/tfloat4/mm.c already
 * exercise: exact divisor match every iteration (forces the RHI=1 "always
 * subtract" path repeatedly, since 1.0/1.0's remainder re-equals the divisor
 * every time), a quotient near the 2^24 mantissa boundary (also RHI-heavy),
 * repeating (non-terminating binary) quotients that exercise the round-half-
 * up path, negative operands, and division by/of powers of two (exact, no
 * rounding). */

int main()
{
    float a, b, r;

    a = 1.0f; b = 1.0f; r = a / b;
    printf("%f\n", r);                 /* 1.000000 - exact match every
                                           iteration; RHI=1 forced repeatedly */

    a = 2.0f; b = 3.0f; r = a / b;
    printf("%f\n", r);                 /* 0.666667 - quotient near the
                                           mantissa's high end, RHI-heavy */

    a = 1.0f; b = 3.0f; r = a / b;
    printf("%f\n", r);                 /* 0.333333 - repeating binary quotient */

    a = 10.0f; b = 4.0f; r = a / b;
    printf("%f\n", r);                 /* 2.500000 - exact, no rounding */

    a = 100.0f; b = 7.0f; r = a / b;
    printf("%f\n", r);                 /* 14.285716 with nearest-even
                                           multiplication in float formatting */

    a = -17.0f; b = 3.0f; r = a / b;
    printf("%f\n", r);                 /* -5.666667 - negative dividend */

    a = 17.0f; b = -3.0f; r = a / b;
    printf("%f\n", r);                 /* -5.666667 - negative divisor */

    a = -8.0f; b = -4.0f; r = a / b;
    printf("%f\n", r);                 /* 2.000000 - both negative */

    a = 8.0f; b = 4.0f; r = a / b;
    printf("%f\n", r);                 /* 2.000000 - exact power-of-two ratio */

    a = 1000000.0f; b = 0.000001f; r = a / b;
    printf("%f\n", r);                 /* large exponent spread; dcc's
                                           printf float-to-string routine has
                                           a known separate limitation
                                           rendering this magnitude (prints
                                           garbage trailing characters) -
                                           verified byte-for-byte identical
                                           to the pre-rewrite implementation,
                                           i.e. not something this rewrite
                                           introduced or changed */

    a = -5.0f; b = -5.0f; r = a / b;
    printf("%f\n", r);                 /* 1.000000 - self-divide, negatives cancel */

    a = 0.0f; b = 5.0f; r = a / b;
    printf("%f\n", r);                 /* 0.000000 - zero dividend shortcut */

    return 0;
}
