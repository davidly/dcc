/*
 * tbcint.c - regression test for BC register residency generalized beyond
 * pointer parameters (find_bc_regalloc_candidate, dcc_func.c): scale_by is
 * a leaf function whose plain `int factor` parameter (not a pointer) is
 * referenced twice, read-only, and never address-taken - it should get
 * loaded into BC once at entry, the same as a qualifying pointer parameter
 * would, since every codegen hook this relies on (emit_load_sym_value_
 * direct, gen_ident's reg_alloc check) treats bc's contents as an opaque
 * 16-bit value regardless of whether it's a pointer or a scalar int.
 */
#include <stdio.h>

int scale_by(int factor)
{
    int total;
    int i;

    total = 0;
    for (i = 0; i < 10; i++) {
        total = total + factor;
        total = total + factor;
    }
    return total;
}

int main(void)
{
    printf("total=%d\n", scale_by(3));
    return 0;
}
