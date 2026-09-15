/* Isolated runtime and structural controls for matrix bit operations. */

#include <stdio.h>

#ifdef MATRIX_BITOPS_UNSIGNED
typedef unsigned int matrix_value;
#else
typedef int matrix_value;
#endif

struct Matrix {
    matrix_value values[2][2];
};

#ifdef MATRIX_BITOPS_RENAMED
#define MATRIX_BITOPS_FUNCTION fixture_matrix_bitops_renamed
#else
#define MATRIX_BITOPS_FUNCTION fixture_matrix_bitops
#endif

#ifdef MATRIX_BITOPS_VOLATILE
#define MATRIX_BITOPS_QUALIFIER volatile
#else
#define MATRIX_BITOPS_QUALIFIER
#endif

#ifdef MATRIX_BITOPS_CHANGED_MULTIPLIER
#define MATRIX_BITOPS_MULTIPLIER 5
#else
#define MATRIX_BITOPS_MULTIPLIER 3
#endif

#ifdef MATRIX_BITOPS_EXTRA_CFG
volatile int matrix_bitops_guard;
#endif

static void MATRIX_BITOPS_FUNCTION(
    MATRIX_BITOPS_QUALIFIER struct Matrix *matrix)
{
    int row;
    int column;

    for (row = 0; row < 2; ++row)
        for (column = 0; column < 2; ++column) {
            matrix->values[row][column] *= MATRIX_BITOPS_MULTIPLIER;
            matrix->values[row][column] -= 1;
            matrix->values[row][column] |= 0x100;
            matrix->values[row][column] &= 0x1ff;
            matrix->values[row][column] ^= 2;
        }
#ifdef MATRIX_BITOPS_EXTRA_CFG
    if (matrix_bitops_guard)
        matrix->values[0][0] = 0;
#endif
}

static unsigned matrix_checksum(const struct Matrix *matrix)
{
    return (unsigned)matrix->values[0][0] +
        2U * (unsigned)matrix->values[0][1] +
        3U * (unsigned)matrix->values[1][0] +
        4U * (unsigned)matrix->values[1][1];
}

int main(void)
{
    struct Matrix matrix = { { { 1, 2 }, { 100, 200 } } };
    unsigned expected;
    int failures = 0;

    MATRIX_BITOPS_FUNCTION(&matrix);
#ifdef MATRIX_BITOPS_CHANGED_MULTIPLIER
    expected = 4227U;
    failures += matrix.values[0][0] != 262;
    failures += matrix.values[0][1] != 267;
    failures += matrix.values[1][0] != 497;
    failures += matrix.values[1][1] != 485;
#else
    expected = 3037U;
    failures += matrix.values[0][0] != 256;
    failures += matrix.values[0][1] != 263;
    failures += matrix.values[1][0] != 297;
    failures += matrix.values[1][1] != 341;
#endif
    failures += matrix_checksum(&matrix) != expected;
    printf(
        "matrix bitops failures=%d checksum=%u values=%u,%u,%u,%u\n",
        failures, matrix_checksum(&matrix),
        (unsigned)matrix.values[0][0],
        (unsigned)matrix.values[0][1],
        (unsigned)matrix.values[1][0],
        (unsigned)matrix.values[1][1]);
    return failures != 0;
}
