/*
 * tnestpi.c - nested pointer postfix side-effect regression.
 *
 * Exercises the three forms from GCC torture execute test 20060929-1.  Dcc's
 * emitters already supported the nested lvalue addresses, but the AST gates
 * rejected pointer-valued postfix dereferences and the address-based postfix
 * helper advanced stored pointers by one byte instead of one element.
 */
#include <stdio.h>

static int fails;

static void assign_nested(int **p, int *q)
{
    *(*p++)++ = *q++;
}

static void read_nested(int **p, int *q)
{
    **p = *q++;
    *(*p++)++;
}

static void update_nested(int **p, int *q)
{
    **p = *q++;
    (*p++)++;
}

static void check(const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s got %d expected %d\n", name, got, expected);
        ++fails;
    }
}

static void check_pointer(const char *name, int *got, int *expected)
{
    if (got != expected) {
        printf("FAIL %s pointer mismatch\n", name);
        ++fails;
    }
}

int main(void)
{
    int values[2];
    int source;
    int *p;

    fails = 0;
    source = 0;

    values[0] = 42;
    values[1] = 99;
    p = values;
    assign_nested(&p, &source);
    check("assign value", values[0], 0);
    check("assign neighbor", values[1], 99);
    check_pointer("assign pointer", p, values + 1);

    values[0] = 43;
    p = values;
    read_nested(&p, &source);
    check("read value", values[0], 0);
    check_pointer("read pointer", p, values + 1);

    values[0] = 44;
    p = values;
    update_nested(&p, &source);
    check("update value", values[0], 0);
    check_pointer("update pointer", p, values + 1);

    if (fails) {
        printf("tnestpi failed: %d\n", fails);
        return 1;
    }
    printf("tnestpi completed with great success\n");
    return 0;
}
