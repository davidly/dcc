/*
 * tmirlife.c - MIR physical-lifetime and helper-ABI regressions.
 */
#include <stdio.h>

#define MAXNAME 18

static int failures;

static void check(int condition, const char *name)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        ++failures;
    }
}

static int late_phi_sink;

static int late_phi_noncall(int flag)
{
    int value;

    if (flag)
        value = 7;
    else
        value = 12;
    late_phi_sink = 0x3456;
    return value + late_phi_sink;
}

static int spill_scratch;

static int spill_helper(int value)
{
    return value * 7 + 1;
}

static int spill_more(int value)
{
    return value > 0;
}

static int spilled_late_phi(int count)
{
    int value;
    int temporary;

    value = 3;
    if (count != 0) {
        for (;;) {
            temporary = spill_helper(count);
            spill_scratch = temporary + count;
            value = value + 2;
            count = count - 1;
            if (!spill_more(count))
                break;
        }
    }
    temporary = spill_helper(5);
    spill_scratch = spill_helper(6);
    spill_scratch = spill_scratch + temporary;
    return value;
}

static volatile int regional_side_effect;

static int identity(int value)
{
    return value;
}

static int regional_address(int value, int take_call)
{
    int local;
    int saved;
    register int blocker;

    saved = value + 17;
    if (take_call)
        identity(value);
    blocker = value;
    regional_side_effect = blocker;
    regional_side_effect = saved;
    *(&local) = saved;
    regional_side_effect = blocker;
    return local;
}

static volatile int observed;

static int dead_definition(int value)
{
    int keep;

    keep = value + 123;
    observed;
    return keep;
}

int spcarg(long value);
int digarg(long value);
long spcret(int value);
long digret(int value);
int uparg(long value);
float upflt(int value);
int sgnspc(int value);
int sgndig(int value);

static int parse_argwide(const char *text, unsigned int *out)
{
    const char *cursor = text;
    unsigned int value;

    while (spcarg((unsigned char)*cursor))
        ++cursor;
    if (*cursor == '-')
        return 0;
    if (*cursor == '+')
        ++cursor;
    if (!digarg((unsigned char)*cursor))
        return 0;

    value = 0;
    while (digarg((unsigned char)*cursor)) {
        unsigned int digit = (unsigned int)(*cursor - '0');
        if (value > 51 || (value == 51 && digit > 1))
            return 0;
        value = (unsigned int)(value * 10 + digit);
        ++cursor;
    }
    while (spcarg((unsigned char)*cursor))
        ++cursor;
    if (*cursor != '\0')
        return 0;

    *out = value;
    return 1;
}

static int parse_retwide(const char *text, unsigned int *out)
{
    const char *cursor = text;
    unsigned int value;

    while (spcret((unsigned char)*cursor))
        ++cursor;
    if (*cursor == '-')
        return 0;
    if (*cursor == '+')
        ++cursor;
    if (!digret((unsigned char)*cursor))
        return 0;

    value = 0;
    while (digret((unsigned char)*cursor)) {
        unsigned int digit = (unsigned int)(*cursor - '0');
        if (value > 51 || (value == 51 && digit > 1))
            return 0;
        value = (unsigned int)(value * 10 + digit);
        ++cursor;
    }
    while (spcret((unsigned char)*cursor))
        ++cursor;
    if (*cursor != '\0')
        return 0;

    *out = value;
    return 1;
}

static int parse_signed_byte(const unsigned char *text, unsigned int *out)
{
    const unsigned char *cursor = text;
    unsigned int value;

    while (sgnspc((signed char)*cursor))
        ++cursor;
    if (*cursor == '-')
        return 0;
    if (*cursor == '+')
        ++cursor;
    if (!sgndig((signed char)*cursor))
        return 0;

    value = 0;
    while (sgndig((signed char)*cursor)) {
        unsigned int digit = (unsigned int)(*cursor - '0');
        if (value > 51 || (value == 51 && digit > 1))
            return 0;
        value = (unsigned int)(value * 10 + digit);
        ++cursor;
    }
    while (sgnspc((signed char)*cursor))
        ++cursor;
    if (*cursor != '\0')
        return 0;

    *out = value;
    return 1;
}

static void uppercase_argwide(char *destination, const char *source)
{
    int index;

    for (index = 0;
         source[index] && index < MAXNAME - 1;
         ++index)
        destination[index] =
            (char)uparg((unsigned char)source[index]);
    destination[index] = '\0';
}

static void uppercase_float(char *destination, const char *source)
{
    int index;

    for (index = 0;
         source[index] && index < MAXNAME - 1;
         ++index)
        destination[index] =
            (char)upflt((unsigned char)source[index]);
    destination[index] = '\0';
}

static int wide_argument_high;

int spcarg(long value)
{
    if ((unsigned long)value > 65535UL)
        ++wide_argument_high;
    return value == ' ' || value == '\t' || value == '\n';
}

int digarg(long value)
{
    if ((unsigned long)value > 65535UL)
        ++wide_argument_high;
    return value >= '0' && value <= '9';
}

long spcret(int value)
{
    return value == ' ' ? 65536L : 0L;
}

long digret(int value)
{
    return value >= '0' && value <= '9' ? 65536L : 0L;
}

int uparg(long value)
{
    if ((unsigned long)value > 65535UL) {
        ++wide_argument_high;
        return '?';
    }
    if (value >= 'a' && value <= 'z')
        return (int)(value - 'a' + 'A');
    return (int)value;
}

float upflt(int value)
{
    if (value >= 'a' && value <= 'z')
        value = value - 'a' + 'A';
    return (float)value;
}

int sgnspc(int value)
{
    return value < 0 ||
        value == ' ' || value == '\t' || value == '\n';
}

int sgndig(int value)
{
    return value >= '0' && value <= '9';
}

static void check_scanner_abis(void)
{
    unsigned int value;
    char upper1[MAXNAME];
    char upper2[MAXNAME];
    unsigned char signed_text[3];

    wide_argument_high = 0;
    value = 0;
    check(parse_argwide("12 ", &value) && value == 12,
          "wide scanner argument");
    uppercase_argwide(upper1, "abcd");
    check(upper1[0] == 'A' && upper1[1] == 'B' &&
          upper1[2] == 'C' && upper1[3] == 'D' &&
          upper1[4] == '\0' && wide_argument_high == 0,
          "wide uppercase argument");

    value = 0;
    check(parse_retwide("1", &value) && value == 1,
          "wide scanner result");
    uppercase_float(upper2, "az");
    check(upper2[0] == 'A' && upper2[1] == 'Z' &&
          upper2[2] == '\0', "float uppercase result");

    signed_text[0] = 255;
    signed_text[1] = '1';
    signed_text[2] = '\0';
    value = 0;
    check(parse_signed_byte(signed_text, &value) && value == 1,
          "signed scanner conversion");
}

int main(void)
{
    int result;

    check(late_phi_noncall(0) == 13410 &&
          late_phi_noncall(1) == 13405,
          "late PHI non-call prefix");

    spill_scratch = 0;
    result = spilled_late_phi(4);
    check(result == 11 && spill_scratch == 79,
          "spilled late PHI slot");

    check(regional_address(1234, 0) == 1251 &&
          regional_address(2345, 1) == 2362,
          "regional address DE preservation");

    observed = 77;
    check(dead_definition(1000) == 1123,
          "dead emitted destination");

    check_scanner_abis();

    if (failures == 0)
        printf("tmirlife: all tests passed\n");
    return failures != 0;
}
