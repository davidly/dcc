/*
 * tnarwinit.c - regression test for a real dcc codegen bug found while
 * narrowing tests/00040.c's for-loop counter to a byte: assigning a
 * constant to a byte-sized local (i = 0) took a fast path that stored the
 * byte and returned WITHOUT ever leaving the (possibly sign/zero-extended)
 * value in HL - fine when the assignment's own result is discarded (the
 * common case), but wrong when it's a subexpression of an enclosing
 * assignment, exactly like the chained `r = i = 0` below once `i` narrows
 * to a byte (a plain int loop counter bounded 0..7 by `i<8`/`i++`, matching
 * try_narrow_for_counter's exact trigger shape). HL held unrelated leftover
 * register contents from the frame setup, which leaked into r's low byte,
 * corrupting the sum computed below.
 */
#include <stdio.h>

int
sumten(void)
{
    int i;
    int r;

    for (r=i=0; i<8; i++)
        r = r + i;

    return r;
}

int main(void)
{
    printf("%d\n", sumten());
    return 0;
}
