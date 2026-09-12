#include <stdio.h>

static char byte;
static unsigned char ubyte;
static int word;
static unsigned int uword;
static _Bool flag;
static volatile float positive_half = 0.5f;
static volatile float negative_half = -0.5f;
static volatile float zero_value = 0.0f;

static float load_value(volatile float *source)
{
    return *source;
}

int main(void)
{
    char *bytep = &byte;
    unsigned char *ubytep = &ubyte;
    int *wordp = &word;
    unsigned int *uwordp = &uword;
    _Bool *flagp = &flag;

    *bytep = 3.75f;
    *ubytep = 253.75f;
    *wordp = -3.75f;
    *uwordp = 259.75f;
    printf("deref float=%d,%u,%d,%u\n", byte, ubyte, word, uword);
    *flagp = load_value(&positive_half);
    printf("deref bool positive=%d\n", flag);
    *flagp = load_value(&negative_half);
    printf("deref bool negative=%d\n", flag);
    *flagp = load_value(&zero_value);
    printf("deref bool zero=%d\n", flag);
    return 0;
}
