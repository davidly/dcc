/* Brace elision for array members, adapted from SDCC's GCC torture execute
 * test 20021118-1.c. */
#include <stdio.h>

struct Values {
    int item[4];
    int tail;
};

static struct Values global_values = { 1, 2, 3, 4, 5 };
static struct Values global_rows[2] = {
    11, 12, 13, 14, 15,
    16, 17, 18, 19, 20
};

int main(void)
{
    struct Values local_values = { 6, 7, 8, 9, 10 };
    struct Values local_rows[2] = {
        21, 22, 23, 24, 25,
        26, 27, 28, 29, 30
    };

    if (global_values.item[0] != 1 || global_values.item[3] != 4 ||
        global_values.tail != 5 || local_values.item[0] != 6 ||
        local_values.item[3] != 9 || local_values.tail != 10 ||
        global_rows[0].item[3] != 14 || global_rows[0].tail != 15 ||
        global_rows[1].item[0] != 16 || global_rows[1].tail != 20 ||
        local_rows[0].item[3] != 24 || local_rows[0].tail != 25 ||
        local_rows[1].item[0] != 26 || local_rows[1].tail != 30) {
        printf("tbrelide failed\n");
        return 1;
    }

    printf("tbrelide completed with great success\n");
    return 0;
}
