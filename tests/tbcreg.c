/*
 * tbcreg.c - regression test for codegen-time BC register residency
 * (find_bc_regalloc_candidate / try_speculative_bc_regalloc_function_body,
 * dcc_func.c): sum_nonzero is a leaf function whose pointer parameter `p`
 * is referenced twice (both under the same `if`), never reassigned, and
 * never has its address taken - it should get loaded into BC once at
 * entry instead of a frame slot, with every later reference reading BC
 * directly rather than reloading from (ix+d).
 */
#include <stdio.h>

int sum_nonzero(char *p, int n)
{
    int total;
    int i;

    total = 0;
    for (i = 0; i < n; i++)
        if (p[i] != 0)
            total = total + p[i];
    return total;
}

int main(void)
{
    char buf[8];
    int i;

    for (i = 0; i < 8; i++)
        buf[i] = i + 1;
    buf[3] = 0;

    printf("sum=%d\n", sum_nonzero(buf, 8));
    return 0;
}
