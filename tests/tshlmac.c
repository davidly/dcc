/* tshlmac (was c8912): regression - a long left-shift whose count comes from
 * an object-like macro (`1L << FP_SHIFT` with `#define FP_SHIFT 16`) must fold
 * exactly like a literal count.  dcc's numeric-macro fast path leaked the
 * previous token's L/U suffix flags onto the expanded constant, and the
 * 16-bit constant folder truncated `(1L << 16) - 1` to -1.
 * Scenario: Q16.16 fixed-point arithmetic using only 32-bit long. */
#include <stdio.h>

#define FP_SHIFT 16
#define FP_ONE (1L << FP_SHIFT)
#define FP_MASK (FP_ONE - 1)

/* Multiply two Q16.16 values without overflowing the 32-bit intermediate. */
static long fp_mul(long a, long b)
{
    long ai;
    long af;
    long bi;
    long bf;

    ai = a >> FP_SHIFT;
    af = a & FP_MASK;
    bi = b >> FP_SHIFT;
    bf = b & FP_MASK;
    return ((ai * bi) << FP_SHIFT) + ai * bf + af * bi + ((af * bf) >> FP_SHIFT);
}

/* Raise a Q16.16 base to an integer power by repeated multiplication. */
static long fp_pow(long base, int exp)
{
    long acc;
    int i;

    acc = FP_ONE;
    for (i = 0; i < exp; i = i + 1) acc = fp_mul(acc, base);
    return acc;
}

static long fp_milli(long x)
{
    return ((x & FP_MASK) * 1000) >> FP_SHIFT;
}

int main(void)
{
    long rate;
    long grown;
    long sq;
    long threehalf;

    /* 1.1 in Q16.16 = 65536 + 6553 (approx). */
    rate = FP_ONE + (FP_ONE / 10);
    grown = fp_pow(rate, 10);          /* ~1.1^10 = 2.593 */
    threehalf = FP_ONE + (FP_ONE / 2);
    sq = fp_mul(threehalf, threehalf); /* 1.5^2 = 2.25 */

    printf("c8912 grow_int=%ld grow_milli=%ld sq_int=%ld sq_milli=%ld\n",
           grown >> FP_SHIFT, fp_milli(grown),
           sq >> FP_SHIFT, fp_milli(sq));
    return 0;
}
