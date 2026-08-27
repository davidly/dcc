/*
 * tfpaext.c - regression coverage for an extern array declared through a
 * function-pointer typedef.  The extern declaration must retain its array
 * element size before a later definition is seen; otherwise table[i] is
 * lowered as an array of the pointed-to function's return type.  A void
 * return made that element size zero and caused an internal MIR fatal.
 * Found via z88dk's testsuite/Issue_497_astroforce_compile.c.
 */
#include <stdio.h>

typedef void (*Action)(int *);

extern Action actions[];

static void invoke(int which, int *value)
{
    (*(actions[which]))(value);
}

static void add_three(int *value)
{
    *value += 3;
}

static void double_it(int *value)
{
    *value *= 2;
}

Action actions[] = { add_three, double_it };

int main(void)
{
    int value = 5;

    invoke(0, &value);
    invoke(1, &value);
    if (value != 16) {
        printf("tfpaext failed: got %d expected 16\n", value);
        return 1;
    }

    printf("tfpaext completed with great success\n");
    return 0;
}
