/* Div/mod results cannot be shared between mutually exclusive blocks. */
#include <stdio.h>

static int checks;
static int failures;

static void ck(int got, int want, const char *name)
{
    ++checks;
    if (got != want) {
        ++failures;
        printf("FAIL %s got=%d want=%d\n", name, got, want);
    }
}

static int signed_switch(int op, int x, int y)
{
    int r;
    switch (op) {
    case 0: r = x / y; break;
    case 1: r = x % y; break;
    default: r = -1; break;
    }
    return r;
}

static int signed_reverse(int op, int x, int y)
{
    int r;
    switch (op) {
    case 0: r = x % y; break;
    case 1: r = x / y; break;
    default: r = -1; break;
    }
    return r;
}

static unsigned int unsigned_switch(int op, unsigned int x, unsigned int y)
{
    unsigned int r;
    switch (op) {
    case 0: r = x / y; break;
    case 1: r = x % y; break;
    default: r = 0; break;
    }
    return r;
}

static unsigned int unsigned_reverse(int op, unsigned int x, unsigned int y)
{
    unsigned int r;
    switch (op) {
    case 0: r = x % y; break;
    case 1: r = x / y; break;
    default: r = 0; break;
    }
    return r;
}

static int conditional(int op, int x, int y)
{
    int r;
    if (op)
        r = x / y;
    else
        r = x % y;
    return r;
}

static int early_return(int op, int x, int y)
{
    if (op)
        return x % y;
    return x / y;
}

int main(void)
{
    /* Enter the second block first, then change operands between calls so
     * a stale stack temporary cannot accidentally contain the right answer. */
    ck(signed_switch(1, 100, 7), 2, "switch remainder first");
    ck(signed_switch(0, 123, 5), 24, "switch quotient");
    ck(signed_switch(1, -100, 7), -2, "switch negative remainder");
    ck(signed_switch(0, 100, -7), -14, "switch negative quotient");
    ck(signed_switch(2, 100, 7), -1, "switch default");

    ck(signed_reverse(1, 100, 7), 14, "reverse quotient first");
    ck(signed_reverse(0, 123, 5), 3, "reverse remainder");
    ck(signed_reverse(1, -100, 7), -14, "reverse negative quotient");
    ck(signed_reverse(0, -100, -7), -2, "reverse negative remainder");
    ck(signed_reverse(2, 100, 7), -1, "reverse default");

    ck(unsigned_switch(1, 60000u, 7u), 3, "unsigned remainder first");
    ck(unsigned_switch(0, 65535u, 41u), 1598, "unsigned quotient");
    ck(unsigned_switch(1, 100u, 7u), 2, "unsigned changed remainder");
    ck(unsigned_reverse(1, 60000u, 7u), 8571, "unsigned reverse first");
    ck(unsigned_reverse(0, 65535u, 41u), 17, "unsigned reverse remainder");
    ck(unsigned_reverse(1, 100u, 7u), 14, "unsigned changed quotient");

    ck(conditional(0, 100, 7), 2, "if remainder first");
    ck(conditional(1, 123, 5), 24, "if quotient");
    ck(conditional(0, -100, 7), -2, "if negative remainder");
    ck(early_return(0, 100, 7), 14, "return quotient first");
    ck(early_return(1, 123, 5), 3, "return remainder");
    ck(early_return(0, -100, 7), -14, "return negative quotient");

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures != 0;
}
