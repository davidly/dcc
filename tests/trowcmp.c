/* Multidimensional array rows decay to pointers in comparison operands.
 * Adapted from SDCC's GCC torture execute test 20080424-1. */
#include <stdio.h>

static int data[16][3][3];
static int row;
static int fails;

static void compare_rows(int left[3][3], int right[3][3])
{
    if (left != data[row + 8])
        ++fails;
    if (right != data[row++])
        ++fails;
}

static void visit_rows(int values[][3][3])
{
    int i;

    for (i = 0; i < 8; ++i)
        compare_rows(values[i + 8], values[i]);
}

int main(void)
{
    row = 0;
    fails = 0;
    visit_rows(data);
    if (row != 8)
        ++fails;

    if (fails) {
        printf("trowcmp failed: %d\n", fails);
        return 1;
    }
    printf("trowcmp completed with great success\n");
    return 0;
}
