#include <stdio.h>

/* A cached 2D-table row must be invalidated when the loop assignment can
 * change its row index through a pointer alias. Keep every stored row in
 * bounds so both cases have fully defined C behavior. */
static int table[2][4] = {
    { 0, 1, 0, 0 },
    { 0, 1, 1, 1 }
};

static int memory_target(void)
{
    int column;
    int row = 0;
    int *row_ptr = &(row);

    for (column = 0; column < 4; ++column)
        row_ptr[0] = table[row][column];
    return row;
}

static int scalar_target(void)
{
    int column;
    int row = 0;
    int *row_ptr = &(row);

    for (column = 0; column < 4; ++column)
        row = table[*row_ptr][column];
    return row;
}

int main(void)
{
    int memory_result = memory_target();
    int scalar_result = scalar_target();

    printf("trowptr memory=%d scalar=%d\n", memory_result, scalar_result);
    return !(memory_result == 1 && scalar_result == 1);
}