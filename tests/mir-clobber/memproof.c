#include <stdio.h>

static int failures;

unsigned int mword(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

unsigned int mcast(const signed char *p)
{
    return (unsigned int)(unsigned char)p[0] |
           ((unsigned int)(unsigned char)p[1] << 8);
}

unsigned int mshare(const unsigned char *p)
{
    unsigned int low = p[0];
    return low + (low | ((unsigned int)p[1] << 8));
}

unsigned int mhigh(const unsigned char *p)
{
    unsigned int low = p[0];
    unsigned int high = (unsigned int)p[1] << 8;
    return (low | high) + high;
}

unsigned int mseven(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 7);
}

unsigned int msign(const signed char *p)
{
    return (unsigned int)p[0] | ((unsigned int)(unsigned char)p[1] << 8);
}

int mrepeat(const unsigned char *p, int index)
{
    return p[index + 1] + p[index + 1] + p[index + 1];
}

int mstore(unsigned char *p, unsigned char *alias, int index)
{
    int before = p[index + 1];
    *alias = 7;
    return before + p[index + 1] + p[index + 1];
}

int mjoin(unsigned char *p, unsigned char *alias, int index, int flag)
{
    int before = p[index + 1];
    if (flag)
        *alias = 9;
    return before + p[index + 1] + p[index + 1];
}

int mloop(unsigned char *p, int index)
{
    int before = p[index + 1];
    int iteration;
    int result = 0;
    for (iteration = 0; iteration < 2; ++iteration) {
        result += p[index + 1] + p[index + 1];
        ++p[index + 1];
    }
    return before + result + p[index + 1];
}

int mvolatile(volatile unsigned char **p, int index)
{
    return (*p)[index + 1] + (*p)[index + 1] + (*p)[index + 1];
}

static void check(const char *name, unsigned int got, unsigned int expected)
{
    if (got != expected) {
        printf("FAIL %s got=%u expected=%u\n", name, got, expected);
        ++failures;
    }
}

int main(void)
{
    static const unsigned char lows[4] = { 0x00, 0x7f, 0x80, 0xff };
    static const unsigned char highs[4] = { 0xff, 0x80, 0x7f, 0x00 };
    unsigned char bytes[2];
    signed char signed_bytes[2];
    volatile unsigned char observed[2];
    volatile unsigned char *pointer = observed;
    unsigned int reference;
    unsigned int signed_low;
    int sample;

    for (sample = 0; sample < 4; ++sample) {
        bytes[0] = lows[sample];
        bytes[1] = highs[sample];
        signed_bytes[0] = (signed char)bytes[0];
        signed_bytes[1] = (signed char)bytes[1];
        reference = (unsigned int)lows[sample] +
                    (unsigned int)highs[sample] * 256U;
        check("word", mword(bytes), reference);
        check("explicit byte cast", mcast(signed_bytes), reference);
        check("shared low", mshare(bytes), reference + lows[sample]);
        check("shared high", mhigh(bytes),
              reference + (unsigned int)highs[sample] * 256U);
        check("shift seven", mseven(bytes),
              (unsigned int)lows[sample] | ((unsigned int)highs[sample] * 128U));
        signed_low = signed_bytes[0] < 0 ?
            (unsigned int)(65280U + lows[sample]) : lows[sample];
        check("signed low", msign(signed_bytes),
              signed_low | ((unsigned int)highs[sample] * 256U));
        check("reuse", mrepeat(bytes, 0), (unsigned int)highs[sample] * 3U);
    }
    bytes[1] = 5;
    check("alias store", mstore(bytes, bytes + 1, 0), 19);
    bytes[1] = 5;
    check("join no write", mjoin(bytes, bytes + 1, 0, 0), 15);
    bytes[1] = 5;
    check("join write", mjoin(bytes, bytes + 1, 0, 1), 23);
    bytes[1] = 2;
    check("backedge write", mloop(bytes, 0), 16);
    observed[1] = 3;
    check("inherited volatile", mvolatile(&pointer, 0), 9);
    printf("MIR memory rewrites checks=33 failures=%d\n", failures);
    return failures != 0;
}
