/*
 * tlmul.c - unsigned long (32-bit) '*' regression coverage, added alongside
 * DCCRTL.MAC's __lmul zero-cross-term skip: the 32x32->32 multiply
 * (base = Xlo*Ylo, plus two 16-bit cross terms Xlo*Yhi and Xhi*Ylo) now
 * skips a cross term entirely whenever its "hi" factor is 0, since it can
 * only contribute 0 - previously __mulu was called anyway, paying for the
 * better part of an 8-iteration loop to re-derive a foregone zero.
 *
 * Profiling tests/pihex.c found __lmul+__mulu together responsible for
 * ~28% of all instructions executed (see tlmod.c's header for __lmu's own
 * ~40%, the companion fix) - a modular-exponentiation workload like
 * powermod16_faster keeps every 32-bit-typed operand bounded by a modulus
 * that starts small and grows, so both operands of most multiplies
 * actually fit in 16 bits (both hi words zero) for a large fraction of
 * calls, degenerating the general 32x32 case to the base term alone.
 *
 * Exercises all four hi-word zero/nonzero combinations (both zero, only
 * Xhi, only Yhi, neither), overflow (mod 2^32) at each combination, and
 * the asymmetric small-times-huge shape. Expected values are literal
 * constants (independently verified in Python), not '*' expressions -
 * using '*' to compute the expected value would only prove __lmul is
 * self-consistent, not correct, since it would route the "want" side
 * through the exact same routine being tested.
 */
#include <stdio.h>

static int checks = 0, failures = 0;
static void okmul(unsigned long a, unsigned long b, unsigned long want)
{
    unsigned long got = a * b;
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %lu * %lu: got %lu want %lu\n", a, b, got, want);
    }
}

int main(void)
{
    /* both operands fit in 16 bits (Xhi==0, Yhi==0): both cross terms
     * skipped, base term alone */
    okmul(0UL, 0UL, 0UL);
    okmul(1UL, 1UL, 1UL);
    okmul(3UL, 5UL, 15UL);
    okmul(65535UL, 1UL, 65535UL);
    okmul(65535UL, 2UL, 131070UL);
    okmul(65535UL, 65535UL, 4294836225UL);
    okmul(40000UL, 40000UL, 1600000000UL);
    okmul(0UL, 65535UL, 0UL);
    okmul(65535UL, 0UL, 0UL);

    /* Xhi!=0, Yhi==0: only the Xhi*Ylo term matters */
    okmul(65536UL, 3UL, 196608UL);
    okmul(100000UL, 7UL, 700000UL);
    okmul(4294967295UL, 1UL, 4294967295UL);
    okmul(4000000000UL, 2UL, 3705032704UL);      /* overflow, mod 2^32 */
    okmul(65537UL, 5UL, 327685UL);

    /* Xhi==0, Yhi!=0: only the Xlo*Yhi term matters (operand order swapped) */
    okmul(3UL, 65536UL, 196608UL);
    okmul(7UL, 100000UL, 700000UL);
    okmul(1UL, 4294967295UL, 4294967295UL);
    okmul(2UL, 4000000000UL, 3705032704UL);
    okmul(5UL, 65537UL, 327685UL);

    /* both Xhi!=0 and Yhi!=0: general case, both cross terms needed */
    okmul(70000UL, 80000UL, 1305032704UL);
    okmul(4294967295UL, 4294967295UL, 1UL);
    okmul(305419896UL, 2271560481UL, 1891143032UL);
    okmul(100000UL, 100000UL, 1410065408UL);
    okmul(3000000000UL, 65539UL, 1987123712UL);

    /* asymmetric: one side small, other side spans all 4 bytes */
    okmul(2UL, 4294967295UL, 4294967294UL);
    okmul(4294967295UL, 2UL, 4294967294UL);
    okmul(32768UL, 32768UL, 1073741824UL);
    okmul(65536UL, 65536UL, 0UL);                /* == 0 mod 2^32 */

    /* powermod16-style repeated squaring: b*b verified via repeated
     * addition, not '*', to stay independent of __lmul itself */
    {
        unsigned long m;
        for (m = 2; m <= 50; m++) {
            unsigned long b = 16UL % m;
            unsigned long bb = b * b;
            unsigned long expect_bb = 0;
            unsigned long i;
            for (i = 0; i < b; i++) expect_bb += b;
            checks++;
            if (bb != expect_bb) { failures++; printf("FAIL b*b for m=%lu: got %lu want %lu\n", m, bb, expect_bb); }
        }
    }

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
