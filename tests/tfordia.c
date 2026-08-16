/*
 * tfordia.c - multidimensional VLA parameter and compound for-condition.
 * Generated archive case: batch8/c9933.
 */
#include <stdio.h>

static int diagonal_sum(int size, const int values[size][3])
{
    int total = 0;

    for (int index = 0; index < size && index < 3; ++index)
        total += values[index][index];
    return total;
}

int main(void)
{
    int values[3][3] = {
        { 2, 0, 0 },
        { 0, 4, 0 },
        { 0, 0, 8 }
    };
    int total = diagonal_sum(3, values);

    printf("tfordia diagonal=%d\n", total);
    if (total == 14)
        printf("tfordia passed with great success\n");
    return total != 14;
}
