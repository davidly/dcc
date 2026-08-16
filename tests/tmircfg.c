/**
 * @file tmircfg.c
 * @brief Smoke-tests MIR lowering and emission for a loop CFG.
 *
 * @par Coverage
 * sum_to() creates a backedge and loop-carried values for both the countdown
 * and accumulator, exercising basic blocks, liveness, and PHI-edge handling.
 */
#include <stdio.h>

static int sum_to(int n)
{
    int sum = 0;

    while (n > 0) {
        sum += n;
        n -= 2;
    }
    return sum;
}

int main(void)
{
    printf("tmircfg %d %d\n", sum_to(5), sum_to(10));
    return 0;
}
