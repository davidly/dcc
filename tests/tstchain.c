#include <stdio.h>

struct Value {
    int first;
    signed int small : 2;
};

static struct Value array[1];
static struct Value source = {5, 1};
static struct Value middle;
static struct Value dest;

int main(void)
{
    int count = -1;

    while (count++ <= 0) {
        struct Value local = {1, -1};
        dest = local = array[0] = source;
        if (local.first != 5 || local.small != 1)
            return 1;
    }
    middle = dest = array[0] = source;
    if (array[0].first != 5 || array[0].small != 1)
        return 2;
    if (dest.first != 5 || dest.small != 1)
        return 3;
    if (middle.first != 5 || middle.small != 1)
        return 4;
    printf("tstchain completed with great success\n");
    return 0;
}
