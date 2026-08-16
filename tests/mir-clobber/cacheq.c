#include <stdio.h>

struct Pad {
    unsigned char bytes[130];
};

static struct Pad pad;

static int calc(void)
{
    return 7;
}

static long combine(int left, long right)
{
    return (long)left + right;
}

static long probe(struct Pad ignored, long wide)
{
    (void)ignored;
    return combine(calc(), wide);
}

int main(void)
{
    printf("%ld\n", probe(pad, 1065L));
    return 0;
}
