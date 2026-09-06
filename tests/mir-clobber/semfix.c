#include <stdio.h>

volatile unsigned char vio[4];
unsigned char plain[4];
static int failures;
static int evaluations;

static void check(const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s: %d expected %d\n", name, got, expected);
        failures++;
    }
}

int vread(int index)
{
    return vio[index + 1] + vio[index + 1] + vio[index + 1];
}

int nread(int index)
{
    return plain[index + 1] + plain[index + 1] + plain[index + 1];
}

unsigned int vword(void)
{
    return (unsigned int)vio[0] | ((unsigned int)vio[1] << 8);
}

int pplus(int index, int *outer)
{
    {
        int *inner = outer;
        return *(inner + index);
    }
}

int pminus(int index, int *outer)
{
    {
        int *inner = outer;
        return *(inner - index);
    }
}

int pcomm(int index, int *outer)
{
    {
        int *inner = outer;
        return *(index + inner);
    }
}

long plong(int index, long *outer)
{
    {
        long *inner = outer;
        return *(inner + index);
    }
}

int pbyte(int index, unsigned char *outer)
{
    {
        unsigned char *inner = outer;
        return *(inner + index);
    }
}

int nextix(int index)
{
    evaluations++;
    return index;
}

int ponce(int index, int *outer)
{
    {
        int *inner = outer;
        return *(inner + nextix(index));
    }
}

int pconst(int *outer)
{
    {
        int *inner = outer;
        return *(inner + 2);
    }
}

int widet(long signed_value, unsigned long unsigned_value, int marker)
{
    return signed_value == -123L && unsigned_value == 65535UL && marker == 77;
}

int widec(int signed_value, unsigned int unsigned_value)
{
    {
        int (*function)(long, unsigned long, int) = widet;
        return function(signed_value, unsigned_value, 77);
    }
}

int chart(signed char value, int marker)
{
    return value == -1 && marker == 91;
}

int charc(int value)
{
    {
        int (*function)(signed char, int) = chart;
        return function(value, 91);
    }
}

int floatt(float value, int marker)
{
    return value == 9.0f && marker == 83;
}

int floatc(int value)
{
    {
        int (*function)(float, int) = floatt;
        return function(value, 83);
    }
}

int main(void)
{
    int words[4];
    long longs[3];

    words[0] = 0x1234;
    words[1] = 0x2345;
    words[2] = 0x3456;
    words[3] = 0x4567;
    longs[0] = 100001L;
    longs[1] = 200002L;
    longs[2] = 300003L;
    vio[0] = 0x12;
    vio[1] = 0x34;
    plain[0] = 0x12;
    plain[1] = 0x34;
    check("pointer plus", pplus(2, words), words[2]);
    check("pointer zero", pplus(0, words), words[0]);
    check("pointer negative", pplus(-1, words + 2), words[1]);
    check("pointer minus", pminus(2, words + 3), words[1]);
    check("integer plus pointer", pcomm(2, words), words[2]);
    check("wide pointer", plong(2, longs) == longs[2], 1);
    check("byte pointer", pbyte(1, plain), 0x34);
    check("offset call", ponce(2, words), words[2]);
    check("offset evaluations", evaluations, 1);
    check("constant offset", pconst(words), words[2]);
    check("wide call", widec(-123, 65535U), 1);
    check("narrow call", charc(-1), 1);
    check("float call", floatc(9), 1);
    check("volatile reads", vread(0), 0x34 * 3);
    check("ordinary reads", nread(0), 0x34 * 3);
    check("volatile word", vword(), 0x3412);
    printf("MIR semantics failures=%d\n", failures);
    return failures != 0;
}