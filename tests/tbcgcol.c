#include <stdio.h>

static int cached_value = 7;

static int global_bc_across_byte_loop(unsigned char start)
{
    register unsigned char count;
    int sum;

    sum = cached_value + cached_value + cached_value;
    count = start;
    while (--count)
        sum++;
    return sum + cached_value + cached_value + cached_value;
}

static int global_bc_across_pointer_loop(const int *values, unsigned char count)
{
    unsigned char index;
    int sum;

    sum = cached_value + cached_value + cached_value;
    for (index = 0; index < count; index++) {
        if (values[index] == 0)
            return sum;
        sum += values[index];
    }
    return sum + cached_value + cached_value + cached_value;
}

int main(void)
{
    int values[4] = { 1, 2, 3, 4 };

    printf("global BC collision: %d\n", global_bc_across_byte_loop(4));
    printf("global BC pointer collision: %d\n",
           global_bc_across_pointer_loop(values, 4));
    return 0;
}