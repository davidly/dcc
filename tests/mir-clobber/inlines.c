#include <stdio.h>

static unsigned char values[8];

static inline void store_sum(
    unsigned int left, unsigned int right, unsigned char value)
{
    values[left + right] = value;
}

int main(int argc, char **argv)
{
    unsigned int left;
    unsigned int right;
    unsigned char value;

    if (argc != 4)
        return 2;
    left = (unsigned int)(argv[1][0] - '0');
    right = (unsigned int)(argv[2][0] - '0');
    value = (unsigned char)(
        (argv[3][0] - '0') * 10 + argv[3][1] - '0');

    store_sum(1, 1, 17);
    store_sum(left, right, value);
    if (values[2] != 17 || values[5] != value || value != 29) {
        printf("inline stores failed %u %u\n", values[2], values[5]);
        return 1;
    }
    printf("inline stores passed\n");
    return 0;
}
