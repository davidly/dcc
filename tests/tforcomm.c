#include <stdio.h>

int main(void)
{
    int values[4] = { 10, 20, 30, 0 };
    int *ptr;
    int i;
    int sum;
    int ticks;

    sum = 0;
    for (i = 0, ptr = values; i < 3; i++, ptr++)
        sum += *ptr;

    printf("both: %d %d %d\n", sum, i, (int)(ptr - values));

    ptr = values;
    sum = 0;
    for (i = 0; i < 3; i += 1, ptr++)
        sum += *ptr;

    printf("iteration: %d %d %d\n", sum, i, (int)(ptr - values));

    sum = 0;
    for (i = 0, ptr = values; i < 3; i++) {
        sum += *ptr;
        ptr++;
    }

    printf("initialization: %d %d %d\n", sum, i, (int)(ptr - values));

    sum = 0;
    ticks = 0;
    ptr = values;
    for (i = 0; ticks++, ptr++, i < 3; i++)
        sum += ptr[-1];

    printf("condition: %d %d %d\n", sum, ticks, (int)(ptr - values));

    sum = 0;
    ticks = 0;
    i = 0;
    while (ticks++, i < 3) {
        sum += values[i];
        i++;
    }

    printf("whilecond: %d %d\n", sum, ticks);

    ptr = values;
    sum = (ptr++, *ptr);
    printf("value: %d %d\n", sum, (int)(ptr - values));

    i = 0;
    ptr = values;
    i++, (void)ptr++;
    printf("statement: %d %d\n", i, (int)(ptr - values));

    return sum == 60 && i == 1 && ptr == values + 1 ? 0 : 1;
}