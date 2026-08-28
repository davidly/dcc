/* Parenthesized array direct-declarators.
 * Adapted from SDCC's GCC torture execute test 930526-1. */
#include <stdio.h>

static int failures;

static void check(const char *name, long got, long expected)
{
    if (got != expected) {
        printf("FAIL %s got %ld expected %ld\n", name, got, expected);
        ++failures;
    }
}

static void exercise(int stride)
{
    int *(p[3]);
    unsigned char *(bytes[2]);
    long *(wide[2][2]);
    int (plain[3]);
    int memory[12];
    unsigned char byte_values[2];
    long long_values[4];
    int i;

    for (i = 0; i < 12; ++i)
        memory[i] = 0;
    for (i = 0; i < 3; ++i)
        p[i] = memory + stride * i;

    p[0][2] = 0x5555;
    p[1][0] = 0x3333;
    p[2][1] = -23456;
    check("pointer row zero", memory[2], 0x5555L);
    check("pointer row one", memory[4], 0x3333L);
    check("pointer row two", memory[9], -23456L);

    byte_values[0] = 17;
    byte_values[1] = 29;
    bytes[0] = &byte_values[0];
    bytes[1] = &byte_values[1];
    check("byte pointer array", *bytes[1], 29L);

    long_values[0] = 100000L;
    long_values[1] = 200000L;
    long_values[2] = 300000L;
    long_values[3] = 400000L;
    wide[0][0] = &long_values[0];
    wide[0][1] = &long_values[1];
    wide[1][0] = &long_values[2];
    wide[1][1] = &long_values[3];
    check("2d pointer array", *wide[1][0], 300000L);

    plain[0] = 3;
    plain[1] = 5;
    plain[2] = 7;
    check("grouped plain array", plain[0] + plain[1] + plain[2], 15L);
}

int main(void)
{
    failures = 0;
    exercise(4);
    if (failures) {
        printf("tparrgrp failed: %d\n", failures);
        return 1;
    }
    printf("tparrgrp completed with great success\n");
    return 0;
}
