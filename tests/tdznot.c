/* Logical not over a constant division by zero on a path that never runs:
 * if (!((255 - 256) % 0)) and return !(5 % 0) were rejected as an
 * unsupported if condition or return expression, though unary -, + and ~
 * over the same operand compiled. */
#include <stdint.h>
#include <stdio.h>

int fails;
int16_t never = 0;
int16_t one = 1;
int32_t out = 7;

static void check(long got, long want, int line)
{
    if (got != want) {
        printf("line %d: got %ld, expected %ld\n", line, got, want);
        ++fails;
    }
}

static int16_t in_if(void)
{
    int16_t r = 3;

    if (never) {
        if (!((255 - 256) % 0))
            r = 4;
        else
            r = 5;
    }
    return r;
}

static int16_t in_return(void)
{
    if (never)
        return !(5 % 0);
    return 6;
}

static int16_t short_circuit(void)
{
    return never && !(5 / 0);
}

static int16_t either(void)
{
    return one || !(70000L % 0L);
}

static int16_t loop(void)
{
    int16_t n = 0;

    while (never && !(5 % 0 + 1))
        ++n;
    return n;
}

static void nested(void)
{
    if (never)
        out = !(1 + 5 % 0) + ~(5 % 0 + 1);
}

int main(void)
{
    check(in_if(), 3, __LINE__);
    check(in_return(), 6, __LINE__);
    check(short_circuit(), 0, __LINE__);
    check(either(), 1, __LINE__);
    check(loop(), 0, __LINE__);
    nested();
    check(out, 7, __LINE__);
    if (fails)
        printf("tdznot failed %d\n", fails);
    else
        printf("tdznot passed\n");
    return 0;
}
