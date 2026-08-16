/*
 * tforjmp.c - VLA parameter and compound for-condition regression.
 * Generated archive case: batch11/c9927.
 */
#include <stdbool.h>
#include <stdio.h>

static bool can_jump(int count, const int jumps[count])
{
    int reach = 0;

    for (int index = 0; index <= reach && index < count; ++index)
        if (index + jumps[index] > reach)
            reach = index + jumps[index];
    return reach >= count - 1;
}

int main(void)
{
    int reachable[6] = { 2, 3, 1, 1, 4, 0 };
    int blocked[5] = { 3, 2, 1, 0, 4 };
    bool can_reach = can_jump(6, reachable);
    bool cannot_reach = can_jump(5, blocked);
    int ok = can_reach && !cannot_reach;

    printf("tforjmp jump=%d,%d\n",
           (int)can_reach, (int)cannot_reach);
    if (ok)
        printf("tforjmp passed with great success\n");
    return !ok;
}
