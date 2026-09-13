#ifdef BUF27_GENERIC

#include <stdio.h>

static volatile unsigned char console_bytes[3];

static int emit_byte(unsigned char value)
{
    return putchar((int)value);
}

int main(void)
{
    int (*writer)(unsigned char) = emit_byte;
    unsigned char index;

    console_bytes[0] = 'O';
    console_bytes[1] = 'K';
    console_bytes[2] = '\n';
    for (index = 0; index < sizeof(console_bytes); ++index)
        if ((*writer)(console_bytes[index]) < 0)
            return 1;
    puts("buffered generic passed");
    return 0;
}

#else

#ifdef BUF27_FASTCALL_CLASS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if BUF27_FASTCALL_CLASS == 0
extern int __fastcall buf27_printf(const char *format, ...);
#define printf buf27_printf
#elif BUF27_FASTCALL_CLASS == 1
static char *__fastcall bf01(size_t n);
#define make_buf bf01
#elif BUF27_FASTCALL_CLASS == 2
extern int __fastcall buf27_setvbuf(
    FILE *stream, char *buffer, int mode, size_t size);
#define setvbuf buf27_setvbuf
#elif BUF27_FASTCALL_CLASS == 3
static void __fastcall bf03(
    const char *buffer, const char *want, const char *tag);
#define expect_prefix bf03
#elif BUF27_FASTCALL_CLASS == 4
extern int __fastcall bf04(FILE *stream);
#define fflush bf04
#elif BUF27_FASTCALL_CLASS == 5
extern void __fastcall bf05(void *pointer);
#define free bf05
#elif BUF27_FASTCALL_CLASS == 6
extern int __fastcall bf06(
    const char *text, FILE *stream);
#define fputs bf06
#elif BUF27_FASTCALL_CLASS == 7
extern int __fastcall bf07(const char *text);
#define puts bf07
#elif BUF27_FASTCALL_CLASS == 8
extern int __fastcall bf08(int value);
#define putchar bf08
#elif BUF27_FASTCALL_CLASS == 9
extern void __fastcall bf09(FILE *stream, char *buffer);
#define setbuf bf09
#elif BUF27_FASTCALL_CLASS == 10
extern int __fastcall buf27_fprintf(
    FILE *stream, const char *format, ...);
#define fprintf buf27_fprintf
#elif BUF27_FASTCALL_CLASS == 11
extern void *__fastcall bf11(
    void *destination, int value, size_t count);
#define memset bf11
#elif BUF27_FASTCALL_CLASS == 12
extern FILE *__fastcall bf12(
    const char *name, const char *mode);
#define fopen bf12
#elif BUF27_FASTCALL_CLASS == 13
extern int __fastcall bf13(FILE *stream);
#define fclose bf13
#elif BUF27_FASTCALL_CLASS == 14
extern int __fastcall bf14(const char *name);
#define remove bf14
#endif

#endif

#include "../tsvbuf2.c"

#endif
