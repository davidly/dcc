/* Runtime bounds in adjusted array parameters are still evaluated on entry.
 * Adapted from SDCC's GCC torture execute test 970217-1.c. */
#include <stdio.h>

static int observed(int count, int values[count++])
{
    (void)values;
    return count;
}

int main(void)
{
    int values[10];

    if (observed(10, values) != 11) {
        printf("tvpapar failed\n");
        return 1;
    }

    printf("tvpapar completed with great success\n");
    return 0;
}
