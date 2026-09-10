#include <stdio.h>

static char byte;
static unsigned char ubyte;
static int word;
static unsigned int uword;
static _Bool flag;

int main(void)
{
    char *bytep = &byte;
    unsigned char *ubytep = &ubyte;
    int *wordp = &word;
    unsigned int *uwordp = &uword;
    _Bool *flagp = &flag;

    *bytep = 259.75f;
    *ubytep = 253.75f;
    *wordp = -3.75f;
    *uwordp = 259.75f;
    *flagp = 259.75f;
    printf("deref float=%d,%u,%d,%u,%d\n",
           byte, ubyte, word, uword, flag);
    *flagp = 0.0f;
    printf("deref bool zero=%d\n", flag);
    return 0;
}
