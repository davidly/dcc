#include <stdio.h>

/* Exercises the fused multiply-add (__fmadd) codegen path: x += a*b for
 * three different lvalue shapes (ix-direct local, non-ix-direct global,
 * and array-element-via-address), plus value combinations spanning the
 * small-integer fast path (mm.c-style), negative operands, mixed
 * magnitude requiring alignment, and a zero product. */

float g_accum;
float g_arr[4];

float local_case(float a, float b, float c)
{
    float x;
    x = c;
    x += a * b;
    return x;
}

float global_case(float a, float b, float c)
{
    g_accum = c;
    g_accum += a * b;
    return g_accum;
}

float array_case(float a, float b, float c, int i)
{
    g_arr[i] = c;
    g_arr[i] += a * b;
    return g_arr[i];
}

int main()
{
    float r;

    r = local_case(2.0f, 3.0f, 4.0f);
    printf("%f\n", r);                 /* 10.000000 - small ints, mm.c-style */

    r = global_case(5.0f, 7.0f, 1.0f);
    printf("%f\n", r);                 /* 36.000000 */

    r = array_case(2.0f, 3.0f, 4.0f, 0);
    printf("%f\n", r);                 /* 10.000000 - array-element lvalue */

    r = local_case(-3.0f, 4.0f, 5.0f);
    printf("%f\n", r);                 /* -7.000000 - negative product */

    r = local_case(3.0f, -4.0f, -5.0f);
    printf("%f\n", r);                 /* -17.000000 - both negative in play */

    r = local_case(0.001f, 1000.0f, 1.0f);
    printf("%f\n", r);                 /* 2.000000 - mixed magnitude,
                                           needs alignment in the add */

    r = local_case(0.0f, 5.0f, 3.0f);
    printf("%f\n", r);                 /* 3.000000 - zero product */

    r = local_case(1.5f, 2.5f, 0.0f);
    printf("%f\n", r);                 /* 3.750000 - zero addend */

    r = local_case(1.9f, 1.0f, 1.9f);
    printf("%f\n", r);                 /* 3.800000 - mantissa-overflow-ish add */

    r = local_case(100000.0f, 100000.0f, 1.0f);
    printf("%f\n", r);                 /* large product, addend negligible */

    return 0;
}
