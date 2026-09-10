/* Focused runtime oracle for the fixed six-dimensional affine-fill schedule. */

#include <stdio.h>

static int exact_values[2][2][2][2][2][2];
static int swapped_values[2][2][2][2][2][2];
static volatile int volatile_values[2][2][2][2][2][2];
static int extent_values[2][2][2][2][2][3];

static int v6(a, b, c, d, e, f)
int a;
int b;
int c;
int d;
int e;
int f;
{
    return a * 32 + b * 16 + c * 8 + d * 4 + e * 2 + f;
}

static int v6_extent(a, b, c, d, e, f)
int a;
int b;
int c;
int d;
int e;
int f;
{
    return a * 48 + b * 24 + c * 12 + d * 6 + e * 3 + f;
}

static void fill_exact(a, base)
int a[2][2][2][2][2][2];
int base;
{
    int a0, a1, a2, a3, a4, a5;

    for (a0 = 0; a0 < 2; ++a0)
    for (a1 = 0; a1 < 2; ++a1)
    for (a2 = 0; a2 < 2; ++a2)
    for (a3 = 0; a3 < 2; ++a3)
    for (a4 = 0; a4 < 2; ++a4)
    for (a5 = 0; a5 < 2; ++a5)
        a[a0][a1][a2][a3][a4][a5] =
            base + v6(a0, a1, a2, a3, a4, a5);
}

static void fill_swapped(a, base)
int a[2][2][2][2][2][2];
int base;
{
    int a0, a1, a2, a3, a4, a5;

    for (a0 = 0; a0 < 2; ++a0)
    for (a1 = 0; a1 < 2; ++a1)
    for (a2 = 0; a2 < 2; ++a2)
    for (a3 = 0; a3 < 2; ++a3)
    for (a4 = 0; a4 < 2; ++a4)
    for (a5 = 0; a5 < 2; ++a5)
        a[a0][a1][a2][a3][a4][a5] =
            base + v6(a1, a0, a2, a3, a4, a5);
}

static void fill_volatile(a, base)
volatile int a[2][2][2][2][2][2];
int base;
{
    int a0, a1, a2, a3, a4, a5;

    for (a0 = 0; a0 < 2; ++a0)
    for (a1 = 0; a1 < 2; ++a1)
    for (a2 = 0; a2 < 2; ++a2)
    for (a3 = 0; a3 < 2; ++a3)
    for (a4 = 0; a4 < 2; ++a4)
    for (a5 = 0; a5 < 2; ++a5)
        a[a0][a1][a2][a3][a4][a5] =
            base + v6(a0, a1, a2, a3, a4, a5);
}

static void fill_extent(a, base)
int a[2][2][2][2][2][3];
int base;
{
    int a0, a1, a2, a3, a4, a5;

    for (a0 = 0; a0 < 2; ++a0)
    for (a1 = 0; a1 < 2; ++a1)
    for (a2 = 0; a2 < 2; ++a2)
    for (a3 = 0; a3 < 2; ++a3)
    for (a4 = 0; a4 < 2; ++a4)
    for (a5 = 0; a5 < 3; ++a5)
        a[a0][a1][a2][a3][a4][a5] =
            base + v6_extent(a0, a1, a2, a3, a4, a5);
}

static int check_values()
{
    int index;
    int *exact = &exact_values[0][0][0][0][0][0];
    int *swapped = &swapped_values[0][0][0][0][0][0];
    volatile int *vol = &volatile_values[0][0][0][0][0][0];
    int *extent = &extent_values[0][0][0][0][0][0];

    for (index = 0; index < 64; ++index) {
        int swapped_index =
            ((index & 16) << 1) + ((index & 32) >> 1) +
            (index & 15);

        if (exact[index] != 100 + index ||
            swapped[index] != 200 + swapped_index ||
            vol[index] != 300 + index)
            return 0;
    }
    for (index = 0; index < 96; ++index)
        if (extent[index] != 400 + index)
            return 0;
    return 1;
}

int main()
{
    fill_exact(exact_values, 100);
    fill_swapped(swapped_values, 200);
    fill_volatile(volatile_values, 300);
    fill_extent(extent_values, 400);
    if (!check_values()) {
        puts("affine fill failed");
        return 1;
    }
    puts("affine fill passed");
    return 0;
}
