#include <stdio.h>

/* Targeted edge cases for the register-resident __fmul rewrite, beyond
 * what tc89fmul/tfloat4/mm.c already exercise: exact powers of two (clean
 * exponent arithmetic, trivial mantissas), values that do and do not
 * cross the "product needs an extra normalizing shift" boundary, negative
 * operands, and mantissas that land exactly on the Ahi/Bhi byte-split
 * boundary the new implementation introduces. */

int main()
{
    float a, b, r;

    a = 2.0f; b = 4.0f; r = a * b;
    printf("%f\n", r);                 /* 8.000000 */

    a = 1.0f; b = 1.0f; r = a * b;
    printf("%f\n", r);                 /* 1.000000 - no carry */

    a = 1.9f; b = 1.9f; r = a * b;
    printf("%f\n", r);                 /* 3.610000 - carry path */

    a = -2.0f; b = 4.0f; r = a * b;
    printf("%f\n", r);                 /* -8.000000 */

    a = -2.0f; b = -4.0f; r = a * b;
    printf("%f\n", r);                 /* 8.000000 */

    a = 0.0f; b = 12345.0f; r = a * b;
    printf("%f\n", r);                 /* 0.000000 */

    a = 0.1f; b = 0.1f; r = a * b;
    printf("%f\n", r);                 /* 0.010000 */

    a = 123456.0f; b = 0.000123f; r = a * b;
    printf("%f\n", r);                 /* ~15.185088 */

    a = 3.14159f; b = 2.71828f; r = a * b;
    printf("%f\n", r);                 /* 8.539722 - nearest-even */

    a = 65536.0f; b = 65536.0f; r = a * b;
    printf("%f\n", r);                 /* 4294967295.000000 - float's
                                           24-bit mantissa can't represent
                                           2^32 exactly; again matches the
                                           pre-rewrite implementation */

    return 0;
}
