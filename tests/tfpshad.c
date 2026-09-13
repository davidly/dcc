#include <stdio.h>

/*
 * An unprototyped local callback is authoritative even when it shadows a
 * differently prototyped global. MIR verification must not import the hidden
 * global's argument types or arity.
 */

int narrow_target(int value)
{
    return value + 1;
}

int wide_target(long left, long right)
{
    return (int)(left + right);
}

int (*callback)(long, long) = wide_target;

int invoke(int (*callback)())
{
    return callback(41);
}

int main(void)
{
    printf("%d\n", invoke(narrow_target));
    printf("%d\n", callback(10L, 20L));
    return 0;
}
