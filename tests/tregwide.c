/* tregwide.c - a wide compare whose left operand is left on the stack
 *
 * When a wide right operand is loaded, the left one can be left pushed for
 * the following binary. A compare that must also save a live register (here
 * the accumulator) pushed it between the two operands, so the runtime helper
 * compared the wrong words. Run with DCC_MIR_PREFER_REGIONAL=1 by
 * compiler-coverage.sh; the default build also checks the value.
 */

#include <stdio.h>

static unsigned count_le(unsigned long *pa, unsigned long *pb)
{
    {
        unsigned acc;
        int k;

        acc = 0;
        for (k = 0; k < 3; ++k)
            acc += (unsigned)((*pa) <= (*pb));
        return acc;
    }
}

int main(void)
{
    unsigned long a = 7UL, b = 4294967295UL;
    unsigned got = count_le(&a, &b);

    if (got != 3)
        printf("FAIL count_le: got %u want 3\n", got);
    else
        printf("tregwide passed\n");
    return got != 3;
}
