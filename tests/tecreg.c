/*
 * tecreg.c - regression test for codegen-time E register residency
 * (gen_local_decl_after_type's g_e_regalloc_claim_active hook, dcc_decl.c;
 * try_speculative_bc_regalloc_function_body, dcc_func.c): fill_bytes is a
 * leaf function whose loop counter `i` is declared with no initializer,
 * compared against a compile-time constant bound, and used solely as the
 * for-loop's own induction variable and an array index (never reused for
 * unrelated arithmetic) - try_narrow_for_counter narrows it to unsigned
 * char, and this feature should then claim it for E for the whole function
 * body instead of a frame slot, with the compare/increment/index all
 * reading E directly.
 *
 * A counter reused for both indexing and unrelated value arithmetic in the
 * same statement (tests/tbig.c's fill_record: `b[i] = (rec+i)&0xff`) is a
 * harder case this round declines safely rather than misapplies - see
 * project memory for that finding. This test sticks to the simpler,
 * succeeding shape deliberately.
 */
#include <stdio.h>

void fill_bytes(char *p)
{
    int i;

    for (i = 0; i < 100; i++)
        p[i] = 1;
}

int main(void)
{
    char buf[100];
    int i;
    int total;

    fill_bytes(buf);

    total = 0;
    for (i = 0; i < 100; i++)
        total = total + buf[i];
    printf("sum=%d\n", total);
    return 0;
}
