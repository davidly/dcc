/*
 * tvlapk.c - bool VLA base-pointer regression.
 * Generated archive case: batch10/c9909.
 */
#include <stdbool.h>
#include <stdio.h>

static int peaks(int rows, int cols, const int values[rows][4])
{
    bool peak[rows][4];
    int count = 0;

    for (int row = 0; row < rows; ++row)
        for (int column = 0; column < cols; ++column) {
            int value = values[row][column];
            peak[row][column] =
                (row == 0 || value > values[row - 1][column]) &&
                (row + 1 == rows || value > values[row + 1][column]) &&
                (column == 0 || value > values[row][column - 1]) &&
                (column + 1 == cols || value > values[row][column + 1]);
            count += peak[row][column];
        }
    return count;
}

int main(void)
{
    int values[4][4] = {
        { 1, 5, 2, 1 },
        { 3, 2, 7, 4 },
        { 2, 8, 1, 6 },
        { 1, 2, 4, 3 }
    };
    int count = peaks(4, 4, values);

    printf("tvlapk peaks=%d\n", count);
    if (count == 6)
        printf("tvlapk passed with great success\n");
    return count != 6;
}
