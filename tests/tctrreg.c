/*
 * tctrreg.c - regression test for pass_byte_for_counter_to_reg_e, the
 * E-register counterpart of pass_byte_for_counter_to_reg_c added when
 * profiling tests/tbig.c against z88dk's zsdcc output: once a loop's
 * pointer base is hoisted into BC (pass_hoist_index_ptr_to_bc), C is no
 * longer free for the loop counter, so this variant keeps it in E
 * instead - matching z88dk's own register allocation for the identical
 * loop shape. The counter must be used BOTH as an index (so a pointer
 * gets hoisted into BC first) AND directly in arithmetic (add a,(ix+O)),
 * with no internal branch (early-return) in the loop body, to exercise
 * this pass specifically rather than pass_hoist_index_ptr_to_bc alone.
 */
#include <stdio.h>

void stamp(char *b, int base)
{
    int i;

    for (i = 0; i < 32; i++)
        b[i] = (char)(base + i);
}

int main(void)
{
    char buf[32];
    int i;
    int total;

    stamp(buf, 5);

    total = 0;
    for (i = 0; i < 32; i++)
        total = total + buf[i];

    printf("total=%d\n", total);
    return 0;
}
