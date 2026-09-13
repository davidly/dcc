#include <stdio.h>

static long left(long value)
{
    return value + 100000L;
}

static long right(long value)
{
    return value + 200000L;
}

static long invoke_designator(int choose, int value)
{
    return (choose ? left : right)(value);
}

static long invoke_address(int choose, int value)
{
    return (choose ? &left : &right)(value);
}

static long invoke_pointer(int choose, int value)
{
    long (*left_pointer)(long) = left;
    long (*right_pointer)(long) = right;

    return (choose ? left_pointer : right_pointer)(value);
}

typedef long (*LongFunction)(long);

static LongFunction make_left(void)
{
    return left;
}

static LongFunction make_right(void)
{
    return right;
}

static long invoke_returned(int choose, int value)
{
    return ((choose ? make_left : make_right)())(value);
}

static long invoke_null_arm(int choose, int value)
{
    return (choose ? left : 0)(value);
}

static long invoke_cast_null_arm(int choose, int value)
{
    return (choose ? left : (void *)0)(value);
}

static long old_style(value)
long value;
{
    return value + 300000L;
}

static long prototyped(long value)
{
    return value + 400000L;
}

static long invoke_mixed(int choose, int value)
{
    return (choose ? old_style : prototyped)(value);
}

typedef long (*OldFunction)();

static OldFunction make_old(void)
{
    return old_style;
}

static LongFunction make_prototyped(void)
{
    return prototyped;
}

static long invoke_mixed_return(int choose, int value)
{
    return ((choose ? make_old : make_prototyped)())(value);
}

int main(void)
{
    int failures = 0;

    if (invoke_designator(1, 7) != 100007L)
        ++failures;
    if (invoke_designator(0, 9) != 200009L)
        ++failures;
    if (invoke_address(1, 11) != 100011L)
        ++failures;
    if (invoke_address(0, 13) != 200013L)
        ++failures;
    if (invoke_pointer(1, -3) != 99997L)
        ++failures;
    if (invoke_pointer(0, -5) != 199995L)
        ++failures;
    if (invoke_returned(1, 11) != 100011L)
        ++failures;
    if (invoke_returned(0, 13) != 200013L)
        ++failures;
    if (invoke_null_arm(1, 15) != 100015L)
        ++failures;
    if (invoke_cast_null_arm(1, 17) != 100017L)
        ++failures;
    if (invoke_mixed(0, 19) != 400019L)
        ++failures;
    if (invoke_mixed_return(1, 21) != 300021L)
        ++failures;
    if (invoke_mixed_return(0, 23) != 400023L)
        ++failures;
    printf("FNPHI33 checks=13 failures=%d\n", failures);
    return failures;
}
