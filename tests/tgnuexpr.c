/* tgnuexpr.c - GNU statement expressions and __builtin_expect. */
#include <stdio.h>

#define likely(expr) __builtin_expect(!!(expr), 1)
#define unlikely(expr) __builtin_expect(!!(expr), 0)

static int failures;

static void check_int(int got, int want, const char *name)
{
    if (got != want) {
        printf("FAIL %s got=%d want=%d\n", name, got, want);
        failures++;
    }
}

static int statement_expression_value(void)
{
    return ({
        int a = 3;
        int b = 4;
        a + b;
    });
}

static int statement_expression_loop(void)
{
    int base = 2;
    return ({
        int i = 0;
        int total = base;
        while (i < 3) {
            total += i;
            i++;
        }
        total;
    });
}

static int statement_expression_if(void)
{
    int value = 7;
    return ({
        int result;
        if (likely(value == 7))
            result = 11;
        else
            result = 13;
        result;
    });
}

static int builtin_expect_value(void)
{
    int x = 0;
    if (unlikely(x))
        return 1;
    if (likely(!x))
        return 2;
    return 3;
}

int main(void)
{
    check_int(statement_expression_value(), 7, "statement expression value");
    check_int(statement_expression_loop(), 5, "statement expression loop");
    check_int(statement_expression_if(), 11, "statement expression if");
    check_int(builtin_expect_value(), 2, "builtin_expect value");

    if (failures == 0)
        printf("test tgnuexpr completed with great success\n");
    return failures;
}