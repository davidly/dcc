/*
 * A conditional expression selecting between void pointers still has a
 * value.  MIR must not confuse its TYPE_VOID base with an actual void
 * expression.  Found via GCC torture pr86231.c.
 */
#include <stdio.h>

#define ONE ((void *)1)
#define TWO ((void *)2)

static int values[8];

static int classify(void *p, int choose_two)
{
    if (p == ONE)
        return 0;
    if (!p)
        p = choose_two ? TWO : ONE;
    return p == ONE ? 0 : 1;
}

int main(void)
{
    if (classify((void *)0, 0) != 0 ||
        classify((void *)0, 1) != 1 ||
        classify(ONE, 0) != 0 || classify(ONE, 1) != 0 ||
        classify(TWO, 0) != 1 || classify(TWO, 1) != 1 ||
        classify(&values[7], 0) != 1 || classify(&values[7], 1) != 1) {
        printf("tptrcond failed\n");
        return 1;
    }
    printf("tptrcond completed with great success\n");
    return 0;
}
