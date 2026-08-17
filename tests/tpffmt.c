/* tpffmt.c - focused printf audit coverage for PF-F4 through PF-F7. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

union raw_float {
    unsigned long bits;
    float value;
};

static char output[400];
static wchar_t wide_text[258];
static wchar_t wide_empty[1];
static char precision_area[518];
static int failures;

static float float_from_bits(unsigned long bits)
{
    union raw_float raw;

    raw.bits = bits;
    return raw.value;
}

static void check(int condition, const char *name)
{
    if (!condition) {
        ++failures;
        printf("FAIL %s\n", name);
    }
}

static void check_string(const char *expected, const char *name)
{
    if (strcmp(output, expected) != 0) {
        ++failures;
        printf("FAIL %s got='%s'\n", name, output);
    }
}

static void test_wide_strings(void)
{
    int i;
    int n;

    for (i = 0; i < 257; ++i)
        wide_text[i] = 'W';
    wide_text[257] = 0;
    wide_empty[0] = 0;

    n = sprintf(output, "Q%5lsZ", wide_text);
    check(n == 259, "wide count");
    check(strlen(output) == 259, "wide length");
    check(output[0] == 'Q' && output[1] == 'W', "wide no wrapped pad");
    check(output[257] == 'W' && output[258] == 'Z' && output[259] == 0,
          "wide tail");

    n = sprintf(output, "abc%ls:%d", wide_empty, 7);
    check_string("abc:7", "wide empty text");
    check(n == 5, "wide empty live count");
}

static void test_bounded_strings(void)
{
    int i;
    int n;

    precision_area[0] = 'L';
    precision_area[1] = 'A';
    precision_area[2] = 'B';
    precision_area[3] = 'C';
    precision_area[4] = 'D';
    for (i = 5; i < 517; ++i)
        precision_area[i] = 'G';
    precision_area[517] = 0;

    n = sprintf(output, "[%.4s]/%d", precision_area + 1, 7);
    check_string("[ABCD]/7", "bounded nonterminated text");
    check(n == 8, "bounded nonterminated count");

    n = sprintf(output, "[%.0s]/%d", precision_area + 1, 8);
    check_string("[]/8", "zero precision text");
    check(n == 4, "zero precision count");
    check(precision_area[0] == 'L' && precision_area[5] == 'G',
          "precision guards");
}

static void test_large_floats(void)
{
    sprintf(output, "%.0f", float_from_bits(0x4f800000UL));
    check_string("4294967296", "float 2^32");

    sprintf(output, "%.0f", float_from_bits(0x4f800001UL));
    check_string("4294967808", "float above 2^32");

    sprintf(output, "%.0f", float_from_bits(0x5f000000UL));
    check_string("9223372036854775808", "float 2^63");

    sprintf(output, "%.0f", float_from_bits(0x7f7fffffUL));
    check_string("340282346638528859811704183484516925440",
                 "float max finite");

    sprintf(output, "%f", float_from_bits(0x4f800000UL));
    check_string("4294967296.000000", "float 2^32 fraction");
}

static void test_space_flag(void)
{
    int n;

    n = sprintf(output, "% d/%d", 1, 2);
    check_string("1/2", "space integer synchronization");
    check(n == 3, "space integer count");

    n = sprintf(output, "% f/%d", float_from_bits(0x3fc00000UL), 7);
    check_string("1.500000/7", "space float selection");
    check(n == 10, "space float count");
}

int main(void)
{
    test_space_flag();
    test_wide_strings();
    test_bounded_strings();
    test_large_floats();

    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures != 0;
}
