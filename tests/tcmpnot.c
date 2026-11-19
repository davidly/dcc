/* A compound division or remainder of a signed long whose right operand
 * is a unary operator over an operation on a nested-block local:
 * g %= ~(1 % k). Only a bare local under the unary marked the operation
 * for deferred retyping, so the unary was assumed int and the remainder
 * stayed signed after repair retyped the unary to unsigned long: 132 %
 * 0xFFFFFFFF became 132 % -1, which is 0. */
#include <stdio.h>

int fails;
long g;
long a[4];
struct S { char pad; long m; } s;

static void check(long got, long want, int line)
{
    if (got != want) {
        printf("line %d: got %ld, expected %ld\n", line, got, want);
        ++fails;
    }
}

static void rem_not(void)
{
    g = 132;
    { unsigned long k = 1; g %= ~(1 % k); }
    check(g, 132, __LINE__);
}

static void rem_not_element(void)
{
    a[3] = 132;
    { unsigned long k = 1; a[3] %= ~(1 % k); }
    check(a[3], 132, __LINE__);
}

static void rem_not_member(void)
{
    s.m = 132;
    { unsigned long k = 3; s.m %= ~(k * 0); }
    check(s.m, 132, __LINE__);
}

static void div_not(void)
{
    g = 132;
    { unsigned long k = 7; g /= ~(1 % k); }
    check(g, 0, __LINE__);
}

static void rem_neg(void)
{
    g = 132;
    { unsigned long k = 5; g %= -(k + 0); }
    check(g, 132, __LINE__);
}

static void rem_not_negative(void)
{
    g = -7;
    { unsigned long k = 1; g %= ~(k - 1); }
    check(g, -7, __LINE__);
}

int main(void)
{
    rem_not();
    rem_not_element();
    rem_not_member();
    div_not();
    rem_neg();
    rem_not_negative();
    if (fails)
        printf("tcmpnot failed %d\n", fails);
    else
        printf("tcmpnot passed\n");
    return 0;
}
