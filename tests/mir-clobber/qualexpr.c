#include <stdio.h>

typedef volatile unsigned char *VByte;
typedef VByte *VDeep;
static unsigned char bytes[4];
static VByte byte_pointer = bytes;

VDeep getdeep(void)
{
    return &byte_pointer;
}

int retdeep(void)
{
    return (*getdeep())[1];
}

static inline VByte incast(unsigned char *pointer)
{
    return (VByte)pointer;
}

int inclone(unsigned char *pointer)
{
    return incast(pointer)[1];
}

int recast(unsigned char *pointer)
{
    return ((VByte)(unsigned char *)(VByte)pointer)[1];
}

int plainret(void);
unsigned char *getplain(void);

int plainret(void)
{
    return getplain()[1];
}

unsigned char *getplain(void)
{
    return bytes;
}

volatile unsigned char *getbyte(void)
{
    return bytes;
}

int indread(volatile unsigned char *(*getter)(void))
{
    return getter()[1];
}

int indlocal(void)
{
    {
        volatile unsigned char *(*getter)(void) = getbyte;
        return getter()[1];
    }
}

typedef volatile unsigned char *(*ByteGetter)(void);
static ByteGetter byte_getter = getbyte;
static unsigned int words[2];

int indglobal(void)
{
    return byte_getter()[1];
}

int indstar(ByteGetter getter)
{
    return (*getter)()[1];
}

volatile unsigned int *getword(int index)
{
    return words + index;
}

unsigned int indword(volatile unsigned int *(*getter)(int), int index)
{
    return getter(index)[1];
}

unsigned char *getraw(volatile int *argument)
{
    (void)argument;
    return bytes;
}

int indplain(unsigned char *(* volatile getter)(volatile int *),
             volatile int *argument)
{
    return getter(argument)[1];
}

static inline volatile unsigned char *inbyte(unsigned char *pointer)
{
    return pointer;
}

int inread(unsigned char *pointer)
{
    return inbyte(pointer)[1];
}

int castdrop(volatile unsigned char *pointer, int index)
{
    return ((unsigned char *)pointer)[index + 1] +
           ((unsigned char *)pointer)[index + 1] +
           ((unsigned char *)pointer)[index + 1];
}

int nested(unsigned char *pointer)
{
    return ((volatile unsigned char *)(unsigned char *)pointer)[1];
}

int voidcast(void *pointer)
{
    return ((VByte)pointer)[1];
}

int deepcast(unsigned char **pointer)
{
    return (*(VDeep)pointer)[1];
}

int castadd(unsigned char *pointer, int index)
{
    return ((volatile unsigned char *)pointer)[index + 1] +
           ((volatile unsigned char *)pointer)[index + 1] +
           ((volatile unsigned char *)pointer)[index + 1];
}

int casttype(unsigned char *pointer, int index)
{
    return ((VByte)pointer)[index + 1] + ((VByte)pointer)[index + 1] +
           ((VByte)pointer)[index + 1];
}

int retread(int index)
{
    return getbyte()[index + 1];
}

int choose(unsigned char *plain, volatile unsigned char *observed, int flag)
{
    return (flag ? plain : observed)[1];
}

int main(void)
{
    int failures = 0;
    int argument = 0;
    unsigned char *pointer = bytes;
    bytes[1] = 7;
    words[1] = 65535U;
    failures += castadd(bytes, 0) != 21;
    failures += casttype(bytes, 0) != 21;
    failures += retread(0) != 7;
    failures += choose(bytes, bytes, 0) != 7;
    failures += choose(bytes, bytes, 1) != 7;
    failures += castdrop(bytes, 0) != 21;
    failures += nested(bytes) != 7;
    failures += voidcast(bytes) != 7;
    failures += deepcast(&pointer) != 7;
    failures += inread(bytes) != 7;
    failures += retdeep() != 7;
    failures += inclone(bytes) != 7;
    failures += recast(bytes) != 7;
    failures += plainret() != 7;
    failures += indread(getbyte) != 7;
    failures += indlocal() != 7;
    failures += indglobal() != 7;
    failures += indstar(getbyte) != 7;
    failures += indword(getword, 0) != 65535U;
    failures += indplain(getraw, &argument) != 7;
    printf("MIR qualifier expressions failures=%d\n", failures);
    return failures != 0;
}