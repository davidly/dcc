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

int abstr(ByteGetter getter)
{
    return ((volatile unsigned char *(*)(void))getter)()[1];
}

int deepind(VDeep (*getter)(void))
{
    return (*getter())[1];
}

typedef VDeep (*DeepGetter)(void);
typedef DeepGetter DeepAlias;
static DeepAlias deep_getter = getdeep;
static ByteGetter getters[2] = { getbyte, getbyte };

struct Readers {
    ByteGetter byte;
    DeepGetter deep;
};

int deepabs(DeepGetter getter)
{
    return (*((VDeep (*)(void))getter)())[1];
}

int deepglob(void)
{
    return (*deep_getter())[1];
}

int deeploc(void)
{
    {
        VDeep (*getter)(void) = getdeep;
        return (*getter())[1];
    }
}

int arrcall(int index)
{
    return getters[index]()[1];
}

int fldcall(struct Readers *reader)
{
    return reader->byte()[1];
}

int flddeep(struct Readers *reader)
{
    return (*reader->deep())[1];
}

VByte getarg(long index)
{
    return bytes + (int)index;
}

int castabi(VByte (*getter)(long), int index)
{
    return ((VByte (*)(long))getter)(index)[1];
}

ByteGetter getfn(void)
{
    return getbyte;
}

int chainret(void)
{
    return getfn()()[1];
}

typedef VByte (*LongGetter)(long);

LongGetter factory(int flag)
{
    (void)flag;
    return getarg;
}

int nestcast(LongGetter (*provider)(int))
{
    return ((LongGetter (*)(int))provider)(1)(1)[1];
}

VByte (*rawfn(int flag))(long)
{
    (void)flag;
    return getarg;
}

int rawcall(void)
{
    return rawfn(1)(1)[1];
}

int rawparam(VByte (*(*provider)(int))(long))
{
    return provider(1)(1)[1];
}

int rawabs(LongGetter (*provider)(int))
{
    return ((VByte (*(*)(int))(long))provider)(1)(1)[1];
}

VByte callcb(VByte (*getter)(long), long index)
{
    return getter(index);
}

int abscb(VByte (*caller)(VByte (*)(long), long), LongGetter getter)
{
    return ((VByte (*)(VByte (*)(long), long))caller)(getter, 1)[1];
}

struct WideReader {
    long (*value)(long);
};
static volatile int observed;

long longval(long value)
{
    return value;
}

void observe(void)
{
    ++observed;
}

unsigned long widecall(struct WideReader *reader, long left, long right)
{
    long first = reader->value(left);
    long second = reader->value(right);
    observe();
    return (unsigned long)first + (unsigned long)second;
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
    struct Readers reader;
    struct WideReader wide_reader;
    bytes[1] = 7;
    bytes[2] = 19;
    reader.byte = getbyte;
    reader.deep = getdeep;
    wide_reader.value = longval;
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
    failures += abstr(getbyte) != 7;
    failures += deepind(getdeep) != 7;
    failures += deepabs(getdeep) != 7;
    failures += deepglob() != 7;
    failures += deeploc() != 7;
    failures += arrcall(1) != 7;
    failures += fldcall(&reader) != 7;
    failures += flddeep(&reader) != 7;
    failures += castabi(getarg, 1) != 19;
    failures += chainret() != 7;
    failures += nestcast(factory) != 19;
    failures += rawcall() != 19;
    failures += rawparam(factory) != 19;
    failures += rawabs(factory) != 19;
    failures += abscb(callcb, getarg) != 19;
    failures += widecall(&wide_reader, 32767L, 1L) != 32768UL;
    failures += widecall(&wide_reader, 65535L, 1L) != 65536UL;
    failures += widecall(&wide_reader, 2147483647L, 1L) != 2147483648UL;
    failures += widecall(&wide_reader, -1L, 1L) != 0UL;
    failures += widecall(&wide_reader, 65536L, 65536L) != 131072UL;
    failures += indword(getword, 0) != 65535U;
    failures += indplain(getraw, &argument) != 7;
    printf("MIR qualifier expressions failures=%d\n", failures);
    return failures != 0;
}