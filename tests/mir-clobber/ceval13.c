#include <stdio.h>

int ce_integer_ops(void)
{
    int i = 0;
    int value = -9;

    while (i < 4) {
        value = value + 13;
        value = value * 3;
        value = value - 5;
        value = value / 2;
        value = value % 97;
        value = (value & 127) | 3;
        value = value ^ 9;
        if (value > 60)
            value = value - 7;
        else
            value = value + 2;
        i = i + 1;
    }
    return value + (value == 53) + (value != 53) +
        (value < 60) + (value <= 60) +
        (value > 40) + (value >= 40);
}

int ce_unsigned_int(void)
{
    int i = 0;
    int result;
    int started_high;
    unsigned value = 65530u;

    started_high = value > 32767u;
    while (i < 3) {
        value = value + 11u;
        i = i + 1;
    }
    result = (int)value;
    result = result + started_high;
    if (value < 100u)
        result = result + 1;
    return result;
}

int ce_signed_long(void)
{
    int i = 0;
    long value = -100000L;

    while (i < 3) {
        value = value + 12345L;
        value = value * -2L;
        value = value / 3L;
        value = value % 20000L;
        i = i + 1;
    }
    return (int)value;
}

int ce_unsigned_long(void)
{
    int i = 0;
    int result;
    int started_high;
    unsigned long value = 4294967280UL;

    started_high = value > 2147483647UL;
    while (i < 2) {
        value = value + 25UL;
        i = i + 1;
    }
    result = (int)value;
    result = result + started_high;
    if (value < 100UL)
        result = result + 1;
    return result;
}

/* DCC deliberately evaluates signed arithmetic with target-width
 * two's-complement wrapping, matching generated Z80 arithmetic. */
int ce_signed_overflow(void)
{
    int i = 0;
    int value = 32767;

    while (i < 1) {
        value = value + 1;
        i = i + 1;
    }
    return value;
}

int ce_signed_sub_overflow(void)
{
    int i = 0;
    int value = -32768;

    while (i < 1) {
        value = value - 1;
        i = i + 1;
    }
    return value;
}

int ce_signed_mul_overflow(void)
{
    int i = 0;
    int value = 20000;

    while (i < 1) {
        value = value * 2;
        i = i + 1;
    }
    return value;
}

int ce_long_overflow(void)
{
    int i = 0;
    long value = 2147483647L;

    while (i < 1) {
        value = value + 2L;
        i = i + 1;
    }
    return (int)value;
}

int ce_long_sub_overflow(void)
{
    int i = 0;
    long value = -2147483647L - 1L;

    while (i < 1) {
        value = value - 2L;
        i = i + 1;
    }
    return (int)value;
}

int ce_long_mul_overflow(void)
{
    int i = 0;
    long value = 1073741825L;

    while (i < 1) {
        value = value * 3L;
        i = i + 1;
    }
    return (int)value;
}

int ce_divide_zero(void)
{
    int i = 0;
    int divisor = 0;
    int value = 12;

    while (i < 1) {
        value = value / divisor;
        i = i + 1;
    }
    return value;
}

int ce_divide_minimum(void)
{
    int i = 0;
    int value = -32768;

    while (i < 1) {
        value = value / -1;
        i = i + 1;
    }
    return value;
}

static int ce_identity(int value)
{
    return value;
}

int ce_call(void)
{
    int i = 0;
    int value = 4;

    while (i < 1) {
        value = ce_identity(value);
        i = i + 1;
    }
    return value;
}

int ce_parameter(int value)
{
    while (value < 1)
        value = value + 1;
    return value;
}

long ce_wide_return(void)
{
    int i = 0;
    long value = 1;

    while (i < 1) {
        value = value + 1L;
        i = i + 1;
    }
    return value;
}

int ce_bounded(void)
{
    int i = 0;

    while (i < 30000)
        i = i + 1;
    return i;
}

int main(void)
{
    printf("consteval13 int=%d uint=%d long=%d ulong=%d\n",
        ce_integer_ops(), ce_unsigned_int(),
        ce_signed_long(), ce_unsigned_long());
    printf("consteval13 wrap=%d,%d,%d longwrap=%d,%d,%d\n",
        ce_signed_overflow(), ce_signed_sub_overflow(),
        ce_signed_mul_overflow(), ce_long_overflow(),
        ce_long_sub_overflow(), ce_long_mul_overflow());
    return 0;
}
