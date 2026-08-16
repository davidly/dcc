/*
 * tvlaero.c - two-dimensional bool VLA base-pointer regression.
 * Generated archive case: batch10/c9927.
 */
#include <stdbool.h>
#include <stdio.h>

static int erode(int rows, int cols, const bool input[rows][6])
{
    bool output[rows][6];
    int count = 0;

    for (int row = 0; row < rows; ++row)
        for (int column = 0; column < cols; ++column) {
            output[row][column] =
                row > 0 && row + 1 < rows &&
                column > 0 && column + 1 < cols &&
                input[row][column] &&
                input[row - 1][column] && input[row + 1][column] &&
                input[row][column - 1] && input[row][column + 1];
            count += output[row][column];
        }
    return count;
}

int main(void)
{
    bool image[5][6] = {
        { 0 },
        { 0, 1, 1, 1, 1, 0 },
        { 0, 1, 1, 1, 1, 0 },
        { 0, 1, 1, 1, 1, 0 },
        { 0 }
    };
    int count = erode(5, 6, image);

    printf("tvlaero erosion=%d\n", count);
    if (count == 2)
        printf("tvlaero passed with great success\n");
    return count != 2;
}
