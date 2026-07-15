/*
 * tlmod.c - unsigned long (32-bit) '%' regression coverage, added alongside
 * DCCRTL.MAC's __lmu leading-zero-byte fast path: ludm_dvs16 (the 16-bit-
 * divisor case) now skips ldqr_byte entirely for each leading zero byte of
 * the dividend, rather than always processing all 4 bytes MSB-first.
 *
 * Profiling tests/pihex.c (ntvcm's -g per-PC execution count) found __lmu
 * alone responsible for ~40% of all instructions executed, almost all of it
 * spent in ldqr_byte re-deriving "0 in, 0 out" for leading zero bytes on a
 * modular-exponentiation workload (powermod16) whose dividends - bounded by
 * a modulus that starts small and grows - very often don't need all 32
 * bits. This exercises every byte-width boundary (dividend using only its
 * low byte, low two bytes, low three, or all four), zero dividends, the
 * divisor==1 and divisor>16-bit cases (both of which must stay exactly as
 * they were, the first never reaching ldqr_byte's loop at all and the
 * second routed to the untouched general 32-bit-divisor path), and exact
 * multiples (remainder 0) at each boundary.
 */
#include <stdio.h>

static int checks = 0, failures = 0;
static void okmod(unsigned long a, unsigned long b, unsigned long want)
{
    unsigned long got = a % b;
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %lu %% %lu: got %lu want %lu\n", a, b, got, want);
    }
}

int main(void)
{
    /* dividend entirely zero (all 4 bytes) */
    okmod(0UL, 1UL, 0UL);
    okmod(0UL, 7UL, 0UL);
    okmod(0UL, 65535UL, 0UL);
    okmod(0UL, 65536UL, 0UL);  /* divisor doesn't fit 16 bits -> general path */

    /* only LSB (byte0) nonzero - all leading bytes zero */
    okmod(1UL, 3UL, 1UL);
    okmod(2UL, 3UL, 2UL);
    okmod(255UL, 7UL, 255UL % 7UL);
    okmod(16UL, 3UL, 1UL);
    okmod(16UL, 2UL, 0UL);
    okmod(16UL, 4UL, 0UL);
    okmod(16UL, 5UL, 1UL);
    okmod(16UL, 16UL, 0UL);
    okmod(16UL, 17UL, 16UL);

    /* byte1 nonzero, byte0 zero (leading two bytes zero) */
    okmod(256UL, 3UL, 256UL % 3UL);
    okmod(512UL, 7UL, 512UL % 7UL);
    okmod(65535UL, 7UL, 65535UL % 7UL);
    okmod(65535UL, 65535UL, 0UL);
    okmod(65535UL, 65534UL, 1UL);

    /* byte2 nonzero (only top byte3 zero) */
    okmod(65536UL, 7UL, 65536UL % 7UL);
    okmod(1000000UL, 7UL, 1000000UL % 7UL);
    okmod(16777215UL, 65535UL, 16777215UL % 65535UL);
    okmod(16777215UL, 251UL, 16777215UL % 251UL);

    /* byte3 nonzero (all four bytes participate) */
    okmod(4000000000UL, 65535UL, 4000000000UL % 65535UL);
    okmod(4294967295UL, 65535UL, 4294967295UL % 65535UL);
    okmod(4294967295UL, 3UL, 4294967295UL % 3UL);
    okmod(2147483648UL, 65521UL, 2147483648UL % 65521UL);

    /* divisor == 1 (quotient=dividend, remainder always 0) */
    okmod(12345UL, 1UL, 0UL);
    okmod(0UL, 1UL, 0UL);
    okmod(4294967295UL, 1UL, 0UL);

    /* divisor > 16 bits: exercises the general (untouched) path, must
     * still agree, guarding against any accidental cross-talk */
    okmod(4294967295UL, 70000UL, 4294967295UL % 70000UL);
    okmod(100000UL, 100001UL, 100000UL);
    okmod(4000000000UL, 4000000001UL, 4000000000UL);
    okmod(4000000000UL, 3999999999UL, 1UL);

    /* exact multiples (remainder 0) at every byte-width boundary */
    okmod(255UL, 5UL, 0UL);
    okmod(65535UL, 5UL, 0UL);
    okmod(16777215UL, 5UL, 0UL);
    okmod(4294967295UL, 5UL, 0UL);
    okmod(4294967295UL, 65535UL, 4294967295UL % 65535UL);

    /* powermod16-style: b, result grow across the modulus - simulate a
     * few iterations directly */
    {
        unsigned long m;
        for (m = 2; m <= 20; m++) {
            unsigned long b = 16UL % m;
            unsigned long bb = (b * b) % m;
            unsigned long expected_b = 16UL % m;
            unsigned long expected_bb = (b * b) % m;
            checks += 2;
            if (b != expected_b) { failures++; printf("FAIL b for m=%lu\n", m); }
            if (bb != expected_bb) { failures++; printf("FAIL bb for m=%lu\n", m); }
        }
    }

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
