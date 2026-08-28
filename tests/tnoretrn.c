#include <stdio.h>
#include <setjmp.h>

static jmp_buf target;

static int jump_away(int value)
{
    longjmp(target, value);
}

int main(void)
{
    int value;

    value = setjmp(target);
    if (value == 0)
        jump_away(23);
    if (value != 23)
        return 1;
    printf("tnoretrn completed with great success\n");
    return 0;
}
