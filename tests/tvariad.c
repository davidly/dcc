/* tvariad.c - C89 variadic function regression test for dcc */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static int sum_i(int n, ...)
{
    va_list ap;
    int i;
    int total;

    total = 0;
    va_start(ap, n);
    for (i = 0; i < n; ++i)
        total = total + va_arg(ap, int);
    va_end(ap);
    return total;
}

static long sum_l(int n, ...)
{
    va_list ap;
    int i;
    long total;

    total = 0L;
    va_start(ap, n);
    for (i = 0; i < n; ++i)
        total = total + va_arg(ap, long);
    va_end(ap);
    return total;
}

static const int macro_values[4][5] = {
    { 11, 13, 17, 19, 23 },
    { 29, 31, 37, 41, 43 },
    { 47, 53, 59, 61, 67 },
    { 71, 73, 79, 83, 89 }
};

static int check_macro_values(const char *channel, int row, ...)
{
    va_list ap;
    int i;
    int ok;

    ok = strcmp(channel, "macro") == 0;
    va_start(ap, row);
    for (i = 0; i < 5; ++i) {
        if (va_arg(ap, int) != macro_values[row][i])
            ok = 0;
    }
    va_end(ap);
    return ok;
}

#define CHECK_MACRO_VALUES(...) check_macro_values("macro", __VA_ARGS__)

int main(void)
{
    int si;
    long sl;
    int ok;

    ok = 1;
    si = sum_i(5, 1, 2, 3, 4, 5);
    if (si != 15) ok = 0;

    sl = sum_l(4, 100000L, 200000L, -30000L, 7L);
    if (sl != 270007L) ok = 0;

    if (!CHECK_MACRO_VALUES(0,
                macro_values[0][0],
                macro_values[0][1],
                macro_values[0][2],
                macro_values[0][3],
                macro_values[0][4])) ok = 0;

    if (!CHECK_MACRO_VALUES(1,
                macro_values[1][0],
                macro_values[1][1],
                macro_values[1][2],
                macro_values[1][3],
                macro_values[1][4])) ok = 0;

    if (!ok) {
        printf("variadic test failed %d %ld\n", si, sl);
        return 1;
    }

    printf("variadic test passed with great success\n");
    return 0;
}
