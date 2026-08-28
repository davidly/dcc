/* tbfprom.c - integer promotion of narrow unsigned bit-fields */

#include <stdio.h>

struct Bits {
    unsigned int small : 3;
    unsigned int large : 15;
};

int main(void)
{
    struct Bits bits;

    printf("tbfprom start\n");
    bits.small = 0;
    bits.large = 0;
    if (bits.small - 2 >= 0 || bits.large - 2 >= 0) {
        printf("FAIL narrow unsigned bit-field promotion\n");
        return 1;
    }
    if ((unsigned int)bits.small - 2 < 0) {
        printf("FAIL explicit unsigned bit-field cast\n");
        return 1;
    }
    printf("tbfprom completed with great success\n");
    return 0;
}
