/* tpragstk.c - source-level stack-check pragma regression. */
#include <stdio.h>

#pragma stack_check(off)

int sink;

int unguarded_leaf(void)
{
    int local[4];

    local[0] = 7;
    sink = local[0];
    return sink;
}

#pragma stack_check ( on )

int guarded_descend(int depth)
{
    int local[8];
    int i;

    for (i = 0; i < 8; ++i)
        local[i] = depth + i;

    sink = local[depth & 7];
    return guarded_descend(depth + 1) + local[0];
}

int main(void)
{
    printf("tpragstk start %d\n", unguarded_leaf());
    sink = guarded_descend(1);
    printf("tpragstk should not reach here %d\n", sink);
    return 0;
}