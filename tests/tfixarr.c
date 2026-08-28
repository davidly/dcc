#include <stdio.h>

static unsigned int values[8];

static void fill(void)
{
    int i;

    for (i = 0; i < 8; ++i)
        values[i] = (float)i;
}

int main(void)
{
    int i;

    fill();
    for (i = 0; i < 8; ++i)
        if (values[i] != (unsigned int)i)
            return 1;
    printf("tfixarr completed with great success\n");
    return 0;
}
