/* ttdarrp.c - pointer to array typedef parameter regression */

#include <stdio.h>

typedef unsigned long LongPair[2];

static int pair_is_all_bits(LongPair *pair)
{
    return (*pair)[0] == ~0UL && (*pair)[1] == ~0UL;
}

int main(void)
{
    LongPair value;

    printf("ttdarrp start\n");
    value[0] = ~0UL;
    value[1] = ~0UL;
    if (!pair_is_all_bits(&value)) {
        printf("FAIL pointer to array typedef\n");
        return 1;
    }
    printf("ttdarrp completed with great success\n");
    return 0;
}
