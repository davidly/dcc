#include <stdio.h>

static unsigned char values[8];

static inline void store_sum(
    unsigned int left, unsigned int right, unsigned char value)
{
    values[left + right] = value;
}

int main(void)
{
    unsigned int left = 2;
    unsigned int right = 3;

    store_sum(1, 1, 17);
    store_sum(left, right, 29);
    if (values[2] != 17 || values[5] != 29) {
        printf("inline stores failed %u %u\n", values[2], values[5]);
        return 1;
    }
    printf("inline stores passed\n");
    return 0;
}
