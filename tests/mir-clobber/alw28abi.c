#include <stdlib.h>

void *w28_allocate(unsigned int size)
{
#ifdef W28_FAIL_SIZE
    if (size == W28_FAIL_SIZE)
        return 0;
#endif
    return malloc(size);
}

void w28_release(void *pointer)
{
    free(pointer);
}

void *w28la(unsigned long size)
{
    return malloc((unsigned int)size);
}

#asm
    public _w28_print
    extrn _printf
_w28_print:
    jp _printf
#endasm
