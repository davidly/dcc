#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if BUF27_FASTCALL_CLASS == 1
char *sw01(size_t size)
{
    return (char *)malloc(size);
}

#asm
        extrn   _sw01
        public  _bf01
_bf01:
        push    hl
        call    _sw01
        pop     bc
        ret
#endasm
#endif

#if BUF27_FASTCALL_CLASS == 3
void sw03(
    const char *buffer, const char *want, const char *tag)
{
    while (*want != 0) {
        if (*buffer != *want) {
            printf("FAIL: fastcall prefix %s\n", tag);
            exit(1);
        }
        ++buffer;
        ++want;
    }
}

#asm
        extrn   _sw03
        public  _bf03
_bf03:
        push    bc
        push    de
        push    hl
        call    _sw03
        pop     bc
        pop     bc
        pop     bc
        ret
#endasm
#endif

#if BUF27_FASTCALL_CLASS == 4
int sw04(FILE *stream)
{
    return fflush(stream);
}

#asm
        extrn   _sw04
        public  _bf04
_bf04:
        push    hl
        call    _sw04
        pop     bc
        ret
#endasm
#endif

#if BUF27_FASTCALL_CLASS == 5
void sw05(void *pointer)
{
    free(pointer);
}

#asm
        extrn   _sw05
        public  _bf05
_bf05:
        push    hl
        call    _sw05
        pop     bc
        ret
#endasm
#endif

#if BUF27_FASTCALL_CLASS == 6
int sw06(const char *text, FILE *stream)
{
    return fputs(text, stream);
}

#asm
        extrn   _sw06
        public  _bf06
_bf06:
        push    de
        push    hl
        call    _sw06
        pop     bc
        pop     bc
        ret
#endasm
#endif

#if BUF27_FASTCALL_CLASS == 7
int sw07(const char *text)
{
    return puts(text);
}

#asm
        extrn   _sw07
        public  _bf07
_bf07:
        push    hl
        call    _sw07
        pop     bc
        ret
#endasm
#endif

#if BUF27_FASTCALL_CLASS == 8
int sw08(int value)
{
    return putchar(value);
}

#asm
        extrn   _sw08
        public  _bf08
_bf08:
        push    hl
        call    _sw08
        pop     bc
        ret
#endasm
#endif

#if BUF27_FASTCALL_CLASS == 9
void sw09(FILE *stream, char *buffer)
{
    setbuf(stream, buffer);
}

#asm
        extrn   _sw09
        public  _bf09
_bf09:
        push    de
        push    hl
        call    _sw09
        pop     bc
        pop     bc
        ret
#endasm
#endif

#if BUF27_FASTCALL_CLASS == 11
void *sw11(void *destination, int value, size_t count)
{
    return memset(destination, value, count);
}

#asm
        extrn   _sw11
        public  _bf11
_bf11:
        push    bc
        push    de
        push    hl
        call    _sw11
        pop     bc
        pop     bc
        pop     bc
        ret
#endasm
#endif

#if BUF27_FASTCALL_CLASS == 12
FILE *sw12(const char *name, const char *mode)
{
    return fopen(name, mode);
}

#asm
        extrn   _sw12
        public  _bf12
_bf12:
        push    de
        push    hl
        call    _sw12
        pop     bc
        pop     bc
        ret
#endasm
#endif

#if BUF27_FASTCALL_CLASS == 13
int sw13(FILE *stream)
{
    return fclose(stream);
}

#asm
        extrn   _sw13
        public  _bf13
_bf13:
        push    hl
        call    _sw13
        pop     bc
        ret
#endasm
#endif

#if BUF27_FASTCALL_CLASS == 14
int sw14(const char *name)
{
    return remove(name);
}

#asm
        extrn   _sw14
        public  _bf14
_bf14:
        push    hl
        call    _sw14
        pop     bc
        ret
#endasm
#endif
