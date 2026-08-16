/*
 * tvlaisle.c - bool VLA indexing regression.
 * Generated archive case: batch11/c9906.
 */
#include <stdbool.h>
#include <stdio.h>

static int count_islands(int rows, int grid[rows][6], int cols)
{
    bool visited[rows][6];
    int islands = 0;
    int stack[64][2];
    int top;

    for (int row = 0; row < rows; ++row)
        for (int column = 0; column < cols; ++column)
            visited[row][column] = false;
    for (int row = 0; row < rows; ++row)
        for (int column = 0; column < cols; ++column)
            if (grid[row][column] && !visited[row][column]) {
                int current_row;
                int current_column;

                ++islands;
                top = 0;
                stack[top][0] = row;
                stack[top++][1] = column;
                while (top) {
                    current_row = stack[--top][0];
                    current_column = stack[top][1];
                    if (current_row < 0 || current_row >= rows ||
                        current_column < 0 || current_column >= cols ||
                        !grid[current_row][current_column] ||
                        visited[current_row][current_column])
                        continue;
                    visited[current_row][current_column] = true;
                    if (top + 4 < 64) {
                        stack[top][0] = current_row - 1;
                        stack[top++][1] = current_column;
                        stack[top][0] = current_row + 1;
                        stack[top++][1] = current_column;
                        stack[top][0] = current_row;
                        stack[top++][1] = current_column - 1;
                        stack[top][0] = current_row;
                        stack[top++][1] = current_column + 1;
                    }
                }
            }
    return islands;
}

int main(void)
{
    int grid[4][6] = {
        { 1, 1, 0, 0, 0, 1 },
        { 1, 0, 0, 1, 1, 0 },
        { 0, 0, 0, 1, 0, 0 },
        { 0, 1, 0, 0, 0, 0 }
    };
    int count = count_islands(4, grid, 6);

    printf("tvlaisle islands=%d\n", count);
    if (count == 4)
        printf("tvlaisle passed with great success\n");
    return count != 4;
}
