/*
 * tbcregno.c - regression test for the two decline paths of
 * find_bc_regalloc_candidate (dcc_func.c): a pointer parameter must still
 * compile and run correctly through the ordinary frame-relative path when
 * it doesn't qualify for BC residency.
 *
 * first_byte declines because p is referenced only once (below
 * BC_REGALLOC_MIN_REFS). addr_taken_sum declines because p's own address
 * is taken (`&p`) - a BC-resident copy would silently desync from that
 * alias, so ident_addr_taken_for must exclude it.
 */
#include <stdio.h>

int first_byte(char *p, int n)
{
    return p[0] + n - n;
}

int addr_taken_sum(char *p, int n)
{
    char **pp;
    int i;
    int total;

    pp = &p;
    total = 0;
    for (i = 0; i < n; i++)
        total = total + (*pp)[i];
    return total;
}

int main(void)
{
    char buf[8];
    int i;

    for (i = 0; i < 8; i++)
        buf[i] = i + 1;

    printf("first=%d\n", first_byte(buf, 8));
    printf("sum=%d\n", addr_taken_sum(buf, 8));
    return 0;
}
