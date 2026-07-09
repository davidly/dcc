/*
 * tptrixld.c - regression test for a real dcc codegen bug: indexing a
 * pointer PARAMETER or LOCAL (not a condition/comparison operand) fell back
 * to a generic "compute the pointer's frame address, then dereference it"
 * sequence (8 instructions) instead of the 2-instruction ld l,(ix+d)/ld
 * h,(ix+d+1) direct load already used for a global pointer and for the same
 * pointer inside an `if` condition. Found via profiling tests/tbig.c against
 * z88dk's zsdcc output. Deliberately plain (non-condition) store and
 * accumulate-read contexts, since those are exactly the shapes that missed
 * the fast path; a bare `if (p[i] != x)` would not have caught this.
 */
#include <stdio.h>

void fill(char *p)
{
    int i;

    for (i = 0; i < 32; i++)
        p[i] = (char)(i * 3);
}

int sum(char *p)
{
    int i;
    int total;

    total = 0;
    for (i = 0; i < 32; i++)
        total = total + p[i];
    return total;
}

int main(void)
{
    char buf[32];

    fill(buf);
    printf("sum=%d\n", sum(buf));
    return 0;
}
