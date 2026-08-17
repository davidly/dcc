/*
 * tconv10.c - Batch 10 conversion edges for strtol/strtoul and the fixed
 * single-byte multibyte/wide-character execution encoding.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>

static int checks;
static int failures;

static void check(int condition, const char *name)
{
    checks++;
    if (!condition) {
        failures++;
        printf("FAIL %s\n", name);
    }
}

static void check_l(const char *name, const char *text, int base,
                    long value, int end_offset)
{
    char *end;
    long result;

    errno = EDOM;
    end = NULL;
    result = strtol(text, &end, base);
    check(result == value && end == text + end_offset && errno == EDOM, name);
}

static void check_ul(const char *name, const char *text, int base,
                     unsigned long value, int end_offset)
{
    char *end;
    unsigned long result;

    errno = EDOM;
    end = NULL;
    result = strtoul(text, &end, base);
    check(result == value && end == text + end_offset && errno == EDOM, name);
}

static void test_integer_conversions(void)
{
    check_l("l bare auto", "0x", 0, 0L, 1);
    check_l("l bad auto", "0xG", 0, 0L, 1);
    check_l("l minus bare", "-0x", 0, 0L, 2);
    check_l("l plus bad", "+0Xz", 0, 0L, 2);
    check_l("l bare b16", "0x", 16, 0L, 1);
    check_l("l bad b16", "-0XG", 16, 0L, 2);
    check_l("l valid auto", "0x1f!", 0, 31L, 4);
    check_l("l valid b16", "-0X2z", 16, -2L, 4);
    check_l("l base8", "0x7", 8, 0L, 1);
    check_l("l base36", "0xG", 36, 1204L, 3);
    check_l("l no digits", "-x", 0, 0L, 0);

    check_ul("ul bare auto", "0x", 0, 0UL, 1);
    check_ul("ul bad auto", "0xG", 0, 0UL, 1);
    check_ul("ul minus bad", "-0xG", 0, 0UL, 2);
    check_ul("ul bare b16", "+0X", 16, 0UL, 2);
    check_ul("ul valid auto", "+0Xf!", 0, 15UL, 4);
    check_ul("ul base36", "0xG", 36, 1204UL, 3);
    check_ul("ul no digits", "+x", 0, 0UL, 0);
}

static void test_wide_conversions(void)
{
    char out[6];
    wchar_t valid[4];
    wchar_t bad0[2];
    wchar_t bad1[3];
    size_t n;

    check(sizeof(size_t) == 2, "size_t is 16 bit");
    check(MB_CUR_MAX == 1, "single byte model");

    out[0] = (char)0x55;
    check(wctomb(out, (wchar_t)0x00ff) == 1 &&
          (unsigned char)out[0] == 0xff, "wctomb high byte");

    out[0] = (char)0x55;
    check(wctomb(out, (wchar_t)0x0100) == -1 &&
          (unsigned char)out[0] == 0x55, "wctomb 0100 reject");

    out[0] = (char)0x55;
    check(wctomb(out, (wchar_t)0x0141) == -1 &&
          (unsigned char)out[0] == 0x55, "wctomb 0141 reject");
    check(wctomb(NULL, (wchar_t)0x0141) == 0, "wctomb null state");

    valid[0] = (wchar_t)'A';
    valid[1] = (wchar_t)0x00ff;
    valid[2] = 0;
    memset(out, 0x55, sizeof(out));
    n = wcstombs(out, valid, sizeof(out));
    check(n == 2 && out[0] == 'A' && (unsigned char)out[1] == 0xff &&
          out[2] == '\0', "wcstombs valid");

    bad0[0] = (wchar_t)0x0100;
    bad0[1] = 0;
    memset(out, 0x55, sizeof(out));
    n = wcstombs(out, bad0, sizeof(out));
    check(n == (size_t)-1 && (unsigned char)out[0] == 0x55 &&
          (unsigned char)out[1] == 0x55, "wcstombs invalid first");

    bad1[0] = (wchar_t)'A';
    bad1[1] = (wchar_t)0x0141;
    bad1[2] = 0;
    memset(out, 0x55, sizeof(out));
    n = wcstombs(out, bad1, sizeof(out));
    check(n == (size_t)-1 && out[0] == 'A' &&
          (unsigned char)out[1] == 0x55, "wcstombs safe prefix");

    memset(out, 0x55, sizeof(out));
    n = wcstombs(out, bad0, 0);
    check(n == 0 && (unsigned char)out[0] == 0x55,
          "wcstombs zero invalid");

    memset(out, 0x55, sizeof(out));
    n = wcstombs(out, valid, 0);
    check(n == 0 && (unsigned char)out[0] == 0x55,
          "wcstombs zero valid");

    memset(out, 0x55, sizeof(out));
    n = wcstombs(out, bad1, 1);
    check(n == 1 && out[0] == 'A' && (unsigned char)out[1] == 0x55,
          "wcstombs bounded prefix");
}

int main(void)
{
    test_integer_conversions();
    test_wide_conversions();

    if (failures == 0)
        printf("tconv10 ok (%d checks)\n", checks);
    else
        printf("tconv10 failed: %d/%d\n", failures, checks);
    return failures ? 1 : 0;
}
