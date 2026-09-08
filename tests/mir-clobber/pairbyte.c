#include <stdio.h>

struct BytePair {
    unsigned char low;
#ifdef MIR_CLOBBER_PAIRED_GAP
    unsigned char gap;
#endif
    unsigned char high;
};

static struct BytePair pair;
static int bias_seed;

static int combine(unsigned int low, unsigned int high)
{
    return low + high * 256U;
}

static int read_pair(const struct BytePair *value)
{
    int bias = bias_seed;
    int result = combine(value->low, value->high);

    return result + bias;
}

int main(void)
{
    pair.low = 0x34;
    pair.high = 0x12;
    bias_seed = 0;
    if (read_pair(&pair) != 0x1234) {
        printf("paired bytes failed\n");
        return 1;
    }
    printf("paired bytes passed\n");
    return 0;
}
