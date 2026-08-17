#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

typedef union {
    float f;
    unsigned long u;
} FU;

static int checks;
static int failures;
static FU last_value;
static char text[620];

static void check_int(const char *name, int got, int want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s got=%d want=%d\n", name, got, want);
    }
}

static void check_long(const char *name, long got, long want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s got=%ld want=%ld\n", name, got, want);
    }
}

static void check_bits(const char *name, unsigned long got,
                       unsigned long want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s got=%08lx want=%08lx\n", name, got, want);
    }
}

static void check_range(const char *name, float got, float low, float high)
{
    checks++;
    if (got < low || got > high) {
        failures++;
        printf("FAIL %s out of range\n", name);
    }
}

static void check_parse(const char *name, const char *input,
                        unsigned long bits, int wanted_errno, int end_offset)
{
    char *end;

    errno = EDOM;
    last_value.f = strtod(input, &end);
    check_bits(name, last_value.u, bits);
    checks++;
    if (errno != wanted_errno) {
        failures++;
        printf("FAIL %s errno got=%d want=%d\n",
               name, errno, wanted_errno);
    }
    checks++;
    if ((int)(end - input) != end_offset) {
        failures++;
        printf("FAIL %s end got=%d want=%d\n",
               name, (int)(end - input), end_offset);
    }
}

static void make_integer_cancel(void)
{
    int i;

    text[0] = '1';
    for (i = 1; i <= 499; ++i)
        text[i] = '0';
    text[500] = 'e';
    text[501] = '-';
    text[502] = '4';
    text[503] = '9';
    text[504] = '9';
    text[505] = '!';
    text[506] = '\0';
}

static void make_fraction_cancel(void)
{
    int i;

    text[0] = '0';
    text[1] = '.';
    for (i = 2; i < 500; ++i)
        text[i] = '0';
    text[500] = '1';
    text[501] = 'e';
    text[502] = '4';
    text[503] = '9';
    text[504] = '9';
    text[505] = '!';
    text[506] = '\0';
}

static void check_whitespace(void)
{
    static const unsigned char whitespace[] = { 9, 10, 11, 12, 13, 32 };
    char number[9];
    int i;

    for (i = 0; i < 6; ++i) {
        number[0] = (char)whitespace[i];
        number[1] = '-';
        number[2] = '4';
        number[3] = '2';
        number[4] = '\0';
        check_int("atoi whitespace", atoi(number), -42);

        number[2] = '1';
        number[3] = '2';
        number[4] = '3';
        number[5] = '4';
        number[6] = '5';
        number[7] = '6';
        number[8] = '\0';
        check_long("atol whitespace", atol(number), -123456L);
    }

    number[0] = 8;
    number[1] = '4';
    number[2] = '2';
    number[3] = '\0';
    check_int("atoi below whitespace", atoi(number), 0);
    check_long("atol below whitespace", atol(number), 0L);
    number[0] = 14;
    check_int("atoi above whitespace", atoi(number), 0);
    check_long("atol above whitespace", atol(number), 0L);
}

int main(void)
{
    FU value;

    check_parse("huge overflow", "1e9999!", 0x7f800000UL, ERANGE, 6);
    check_parse("negative huge overflow", "-1e9999!",
                0xff800000UL, ERANGE, 7);
    check_parse("huge underflow", "1e-9999!", 0x00000000UL, ERANGE, 7);
    check_parse("negative huge underflow", "-1e-9999!",
                0x80000000UL, ERANGE, 8);
    check_parse("zero huge exponent", "-0e9999!",
                0x80000000UL, EDOM, 7);
    check_parse("explicit infinity", "inf!", 0x7f800000UL, EDOM, 3);
    check_parse("negative infinity", "-infinity!",
                0xff800000UL, EDOM, 9);
    check_parse("explicit nan", "nan!", 0x7fc00000UL, EDOM, 3);

    check_parse("overflow boundary", "4e38!",
                0x7f800000UL, ERANGE, 4);
    check_parse("negative overflow boundary", "-4e38!",
                0xff800000UL, ERANGE, 5);
    check_parse("underflow boundary", "1e-38!",
                0x00000000UL, ERANGE, 5);
    check_parse("negative underflow boundary", "-1e-38!",
                0x80000000UL, ERANGE, 6);
    check_parse("finite high boundary", "3e38!",
                0x7f61b1e4UL, EDOM, 4);
    check_range("finite high value", last_value.f, 2.99e38f, 3.01e38f);
    check_parse("finite low boundary", "2e-38!",
                0x00d9c7dcUL, EDOM, 5);
    check_range("finite low value", last_value.f, 1.99e-38f, 2.01e-38f);

    check_parse("bad exponent endpoint", "12e+!",
                0x41400000UL, EDOM, 2);
    check_parse("no conversion endpoint", " \n+",
                0x00000000UL, EDOM, 0);

    make_integer_cancel();
    check_parse("integer cancellation", text,
                0x3f7ffffcUL, EDOM, 505);
    check_range("integer cancellation value", last_value.f,
                0.99999f, 1.00001f);
    errno = EDOM;
    value.f = atof(text);
    check_bits("atof integer cancellation", value.u, 0x3f7ffffcUL);
    check_int("atof cancellation errno", errno, EDOM);

    make_fraction_cancel();
    check_parse("fraction cancellation", text,
                0x3f800000UL, EDOM, 505);
    check_range("fraction cancellation value", last_value.f,
                0.99999f, 1.00001f);

    errno = EDOM;
    value.f = atof("4e38");
    check_bits("atof overflow value", value.u, 0x7f800000UL);
    check_int("atof overflow errno", errno, EDOM);

    check_whitespace();

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
