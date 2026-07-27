/* Constant-divisor signed/unsigned division and remainder coverage.
 *
 * dccpeep's pass_const_divmod_helpers rewrites a divide or modulo by a
 * compile-time constant into one of the specialised runtime helpers, so each
 * family needs a dividend range that includes negatives and the 16-bit
 * extremes:
 *
 *   %  power-of-two divisor <= 256    -> __r1p  (mask + double negate)
 *   %  other divisor <= 255           -> __r1s / __r1u  (8-bit division)
 *   %  divisor > 255                  -> __r2s / __r2u  (16-bit division)
 *   /  power-of-two divisor <= 16384  -> __q1p  (shift + sign restore)
 *   /  other divisor                  -> __q2s / __q2u  (16-bit division)
 *
 * C truncates toward zero, which is what neither a plain mask nor a plain
 * arithmetic shift does for a negative dividend: -10 % 8 is -2 rather than 6,
 * and -1 / 2 is 0 rather than -1.
 */

#include <stdio.h>

int vals[] = { 0, 1, 2, 7, 8, 9, 15, 16, 17, 100, 127, 128, 129, 255, 256, 257,
               1000, 4095, 32767,
               -1, -2, -7, -8, -9, -15, -16, -17, -100, -127, -128, -129,
               -255, -256, -257, -1000, -4095, -32767, -32768 };

unsigned int uvals[] = { 0, 1, 7, 8, 9, 255, 256, 257, 1000, 32767, 32768,
                         65534, 65535 };

int main()
{
    int j;
    long sum;

    sum = 0;

    /* signed, power-of-two divisors: the whole mask range 1..255 */
    for (j = 0; j < (int)(sizeof(vals) / sizeof(vals[0])); j++) {
        int v = vals[j];
        printf("p2 %d: %d %d %d %d %d %d %d %d\n", v,
               v % 2, v % 4, v % 8, v % 16,
               v % 32, v % 64, v % 128, v % 256);
        sum += v % 2;  sum += v % 4;   sum += v % 8;   sum += v % 16;
        sum += v % 32; sum += v % 64;  sum += v % 128; sum += v % 256;
    }

    /* signed, non-power-of-two divisors: small ones and one above 255 */
    for (j = 0; j < (int)(sizeof(vals) / sizeof(vals[0])); j++) {
        int v = vals[j];
        printf("np %d: %d %d %d %d %d %d\n", v,
               v % 3, v % 5, v % 10, v % 100, v % 255, v % 1000);
        sum += v % 3;   sum += v % 5;   sum += v % 10;
        sum += v % 100; sum += v % 255; sum += v % 1000;
    }

    /* unsigned, across the 8-bit/16-bit helper boundary */
    for (j = 0; j < (int)(sizeof(uvals) / sizeof(uvals[0])); j++) {
        unsigned int u = uvals[j];
        printf("un %u: %u %u %u %u %u\n", u,
               u % 8, u % 3, u % 255, u % 256, u % 1000);
        sum += (long)(u % 8);   sum += (long)(u % 3);
        sum += (long)(u % 255); sum += (long)(u % 256);
        sum += (long)(u % 1000);
    }

    /* signed division by powers of two, including the shift counts that
     * cross the byte-move boundary (8) and the largest positive 16-bit
     * power of two (16384) */
    for (j = 0; j < (int)(sizeof(vals) / sizeof(vals[0])); j++) {
        int v = vals[j];
        printf("dp %d: %d %d %d %d %d %d %d %d %d\n", v,
               v / 2, v / 4, v / 8, v / 128, v / 256,
               v / 512, v / 1024, v / 4096, v / 16384);
        sum += v / 2;    sum += v / 4;    sum += v / 8;
        sum += v / 128;  sum += v / 256;  sum += v / 512;
        sum += v / 1024; sum += v / 4096; sum += v / 16384;
    }

    /* signed and unsigned division by non-powers of two */
    for (j = 0; j < (int)(sizeof(vals) / sizeof(vals[0])); j++) {
        int v = vals[j];
        printf("dn %d: %d %d %d %d\n", v, v / 3, v / 10, v / 255, v / 1000);
        sum += v / 3; sum += v / 10; sum += v / 255; sum += v / 1000;
    }
    for (j = 0; j < (int)(sizeof(uvals) / sizeof(uvals[0])); j++) {
        unsigned int u = uvals[j];
        printf("du %u: %u %u %u %u\n", u, u / 8, u / 3, u / 256, u / 1000);
        sum += (long)(u / 8);   sum += (long)(u / 3);
        sum += (long)(u / 256); sum += (long)(u / 1000);
    }

    printf("sum %ld\n", sum);
    return 0;
}
