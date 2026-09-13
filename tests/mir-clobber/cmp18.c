#include <stdio.h>

static int l_eq(long left, long right)
{
    if (left == right)
        return 11;
    return 22;
}

static int l_ne(long left, long right)
{
    if (left != right)
        return 11;
    return 22;
}

static int l_lt(long left, long right)
{
    if (left < right)
        return 11;
    return 22;
}

static int l_ge(long left, long right)
{
    if (left >= right)
        return 11;
    return 22;
}

static int l_gt(long left, long right)
{
    if (left > right)
        return 11;
    return 22;
}

static int l_le(long left, long right)
{
    if (left <= right)
        return 11;
    return 22;
}

static int ul_lt(unsigned long left, unsigned long right)
{
    if (left < right)
        return 11;
    return 22;
}

static int ul_ge(unsigned long left, unsigned long right)
{
    if (left >= right)
        return 11;
    return 22;
}

static int f_eq(float left, float right)
{
    if (left == right)
        return 11;
    return 22;
}

static int f_ne(float left, float right)
{
    if (left != right)
        return 11;
    return 22;
}

static int f_lt(float left, float right)
{
    if (left < right)
        return 11;
    return 22;
}

static int f_ge(float left, float right)
{
    if (left >= right)
        return 11;
    return 22;
}

static int f_gt(float left, float right)
{
    if (left > right)
        return 11;
    return 22;
}

static int f_le(float left, float right)
{
    if (left <= right)
        return 11;
    return 22;
}

static int truth_long(long value)
{
    if (value)
        return 11;
    return 22;
}

static int truth_ulong(unsigned long value)
{
    if (value)
        return 11;
    return 22;
}

static int truth_float(float value)
{
    if (value)
        return 11;
    return 22;
}

static int truth_pointer(int *value)
{
    if (value)
        return 11;
    return 22;
}

static int pointer_eq(int *left, int *right)
{
    if (left == right)
        return 11;
    return 22;
}

int main(void)
{
    int first;
    int second;
    int failures = 0;

    failures += l_eq(7L, 7L) != 11 || l_eq(7L, -7L) != 22;
    failures += l_ne(7L, 7L) != 22 || l_ne(7L, -7L) != 11;
    failures += l_lt(-2147483647L - 1L, 2147483647L) != 11;
    failures += l_ge(-1L, 1L) != 22 || l_ge(1L, -1L) != 11;
    failures += l_gt(1L, -1L) != 11 || l_gt(-1L, 1L) != 22;
    failures += l_le(-1L, 1L) != 11 || l_le(1L, -1L) != 22;
    failures += ul_lt(0UL, 4294967295UL) != 11;
    failures += ul_ge(4294967295UL, 0UL) != 11;
    failures += f_eq(1.5f, 1.5f) != 11 || f_eq(1.5f, 2.5f) != 22;
    failures += f_ne(1.5f, 1.5f) != 22 || f_ne(1.5f, 2.5f) != 11;
    failures += f_lt(-1.5f, 2.5f) != 11 || f_lt(2.5f, -1.5f) != 22;
    failures += f_ge(-1.5f, 2.5f) != 22 || f_ge(2.5f, -1.5f) != 11;
    failures += f_gt(2.5f, -1.5f) != 11 || f_gt(-1.5f, 2.5f) != 22;
    failures += f_le(-1.5f, 2.5f) != 11 || f_le(2.5f, -1.5f) != 22;
    failures += truth_long(0L) != 22 || truth_long(-1L) != 11;
    failures += truth_ulong(0UL) != 22 || truth_ulong(1UL) != 11;
    failures += truth_float(0.0f) != 22 || truth_float(-0.0f) != 22;
    failures += truth_float(1.0f) != 11;
    failures += truth_pointer((int *)0) != 22 ||
        truth_pointer(&first) != 11;
    failures += pointer_eq(&first, &first) != 11 ||
        pointer_eq(&first, &second) != 22;
    printf("comparison18 failures=%d\n", failures);
    return failures != 0;
}
