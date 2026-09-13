#include <stdio.h>

static _Bool flag;
static volatile float positive_half = 0.5f;
static volatile float negative_half = -0.5f;
static volatile float zero_value = 0.0f;

static void store_bool(_Bool *destination, float value)
{
    *destination = value;
}

int main(void)
{
    store_bool(&flag, positive_half);
    printf("bool pointer positive=%d\n", flag);
    store_bool(&flag, negative_half);
    printf("bool pointer negative=%d\n", flag);
    store_bool(&flag, zero_value);
    printf("bool pointer zero=%d\n", flag);
    return 0;
}
