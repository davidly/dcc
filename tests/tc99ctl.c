/* tc99ctl.c - C99 control-expression regressions distilled from c-testsuite. */
#include <stdio.h>

static int failures;
static int effects;

static void chk(int got, int want, const char *name)
{
    if (got != want) {
        printf("FAIL %s got=%d want=%d\n", name, got, want);
        failures++;
    }
}

static int side(int value)
{
    effects++;
    return value;
}

static void test_constant_expression_arrays(void)
{
    goto start;
    {
        int a[1 && 1];
        int b[0 || 1];
        int c[0 ? 2 : 1];
    start:
        a[0] = 11;
        b[0] = 12;
        c[0] = 13;

        chk(a[0], 11, "logical-and bound");
        chk(b[0], 12, "logical-or bound");
        chk(c[0], 13, "conditional bound");
        chk((int)(sizeof a / sizeof a[0]), 1, "sizeof a");
        chk((int)(sizeof b / sizeof b[0]), 1, "sizeof b");
        chk((int)(sizeof c / sizeof c[0]), 1, "sizeof c");
    }
}

static void test_short_circuit(void)
{
    int r;

    effects = 0;
    r = 0 ? side(1) : 11;
    chk(r, 11, "false conditional result");
    chk(effects, 0, "false conditional skips lhs");

    effects = 0;
    r = 1 ? 12 : side(2);
    chk(r, 12, "true conditional result");
    chk(effects, 0, "true conditional skips rhs");

    effects = 0;
    r = 0 && side(3);
    chk(r, 0, "and false result");
    chk(effects, 0, "and false skips rhs");

    effects = 0;
    r = 1 || side(4);
    chk(r, 1, "or true result");
    chk(effects, 0, "or true skips rhs");

    effects = 0;
    r = 1 && side(5);
    chk(r, 1, "and true result");
    chk(effects, 1, "and true evaluates rhs");

    effects = 0;
    r = 0 || side(6);
    chk(r, 1, "or false result");
    chk(effects, 1, "or false evaluates rhs");
}

int main(void)
{
    test_constant_expression_arrays();
    test_short_circuit();

    if (failures == 0)
        printf("tc99ctl completed with great success\n");
    return failures;
}