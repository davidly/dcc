/* Exhaustive exact-schedule fixture for 2x2 matrix multiplication. */
#include <stdio.h>

#ifdef MATMUL_UNSIGNED
typedef unsigned matrix_value_t;
#else
typedef int matrix_value_t;
#endif

struct Matrix {
#ifdef MATMUL_VOLATILE_VALUES
    volatile matrix_value_t values[2][2];
#else
    matrix_value_t values[2][2];
#endif
};

#ifdef MATMUL_RENAMED
#define matrix_multiply_fixture matrix_multiply_fixture_renamed
#endif

static struct Matrix matrix_multiply_fixture(
    struct Matrix a, struct Matrix b)
{
    struct Matrix r = { { { 0, 0 }, { 0, 0 } } };
    int i;
    int j;
    int k;

#ifdef MATMUL_EXTRA_CFG
    if (a.values[0][0] == 30000)
        return b;
#endif
    for (i = 0; i < 2; ++i)
        for (j = 0; j < 2; ++j)
            for (k = 0; k < 2; ++k)
                r.values[i][j] += a.values[i][k] * b.values[k][j];
    return r;
}

static int check_case(
    struct Matrix left, struct Matrix right,
    int e00, int e01, int e10, int e11)
{
    struct Matrix result;

    result = matrix_multiply_fixture(left, right);
    return result.values[0][0] != e00 ||
        result.values[0][1] != e01 ||
        result.values[1][0] != e10 ||
        result.values[1][1] != e11;
}

int main(void)
{
    struct Matrix first_left = { { { 1, 2 }, { 3, 4 } } };
    struct Matrix first_right = { { { 2, 0 }, { 1, 2 } } };
    struct Matrix second_left = { { { 7, 5 }, { 2, 6 } } };
    struct Matrix second_right = { { { 3, 4 }, { 8, 1 } } };
    int failures;

    failures = check_case(first_left, first_right, 4, 4, 10, 8);
    failures += check_case(
        second_left, second_right, 61, 33, 54, 14);
    printf("matrix multiply failures=%d checksum=%d\n",
           failures, 4 + 4 * 3 + 10 * 5 + 8 * 7 +
           61 * 11 + 33 * 13 + 54 * 17 + 14 * 19);
    return failures != 0;
}
