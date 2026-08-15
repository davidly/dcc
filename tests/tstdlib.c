/* tstdlib.c - stdlib.h regression coverage, including atoi()/atol() etc.
 * wraparound at dcc's Z80 target int/long widths.
 *
 * Host validation is skipped (tests/_test_overrides.json's "host": true):
 * the *wrap16* checks deliberately overflow a 16-bit `int`, matching dcc's
 * target where `int` is 2 bytes. A host's `int` is 4 bytes even under a
 * 32-bit (-m32) compile - unlike `long`, there's no host compiler mode
 * that reproduces a 16-bit `int`, so this can't be validated on any host.
 */
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

static void fail_int(const char *name, int got, int expected)
{
    printf("FAIL %s got %d expected %d\n", name, got, expected);
    failures++;
}

static void fail_long(const char *name, long got, long expected)
{
    printf("FAIL %s got %ld expected %ld\n", name, got, expected);
    failures++;
}

static void check_int(const char *name, int got, int expected)
{
    if (got != expected)
        fail_int(name, got, expected);
}

static void check_long(const char *name, long got, long expected)
{
    if (got != expected)
        fail_long(name, got, expected);
}

static void check_div(const char *name, int numer, int denom,
                      int expected_quot, int expected_rem)
{
    div_t result;
    result = div(numer, denom);
    if (result.quot != expected_quot)
        fail_int(name, result.quot, expected_quot);
    if (result.rem != expected_rem)
        fail_int(name, result.rem, expected_rem);
    if (result.quot * denom + result.rem != numer)
        fail_int(name, result.quot * denom + result.rem, numer);
}

static void check_ldiv(const char *name, long numer, long denom,
                       long expected_quot, long expected_rem)
{
    ldiv_t result;
    result = ldiv(numer, denom);
    if (result.quot != expected_quot)
        fail_long(name, result.quot, expected_quot);
    if (result.rem != expected_rem)
        fail_long(name, result.rem, expected_rem);
    if (result.quot * denom + result.rem != numer)
        fail_long(name, result.quot * denom + result.rem, numer);
}

int main(void)
{
    check_int("abs0", abs(0), 0);
    check_int("abspos", abs(123), 123);
    check_int("absneg", abs(-123), 123);
    check_int("abswide", abs(-32767), 32767);

    check_long("labs0", labs(0L), 0L);
    check_long("labspos", labs(123456L), 123456L);
    check_long("labsneg", labs(-123456L), 123456L);
    check_long("labswide", labs(-2147483647L), 2147483647L);

    check_div("divpos", 7, 3, 2, 1);
    check_div("divneg1", -7, 3, -2, -1);
    check_div("divneg2", 7, -3, -2, 1);
    check_div("divneg3", -7, -3, 2, -1);
    check_div("divzero", 0, 5, 0, 0);

    check_ldiv("ldivpos", 200000L, 7L, 28571L, 3L);
    check_ldiv("ldivneg1", -200000L, 7L, -28571L, -3L);
    check_ldiv("ldivneg2", 200000L, -7L, -28571L, 3L);
    check_ldiv("ldivneg3", -200000L, -7L, 28571L, -3L);
    check_ldiv("ldivzero", 0L, 13L, 0L, 0L);

    /* atoi: 16-bit decimal parse with sign, whitespace, and trailing junk */
    check_int("atoi0", atoi("0"), 0);
    check_int("atoipos", atoi("123"), 123);
    check_int("atoineg", atoi("-123"), -123);
    check_int("atoiplus", atoi("+456"), 456);
    check_int("atoiws", atoi("   789"), 789);
    check_int("atoitab", atoi("\t-42"), -42);
    check_int("atoijunk", atoi("123abc"), 123);
    check_int("atoinodig", atoi("abc"), 0);
    check_int("atoiempty", atoi(""), 0);
    check_int("atoiwide", atoi("32767"), 32767);
    check_int("atoimin", atoi("-32768"), -32767 - 1);
    check_int("atoinegzero", atoi("  -0"), 0);
    check_int("atoiwrap16", atoi("65536"), 0);          /* 2^16 wraps to 0 */
    check_int("atoiwrap16p1", atoi("65537"), 1);        /* 2^16+1 wraps to 1 */
    check_int("atoiwrapbig", atoi("99999"), -31073);    /* 99999 mod 2^16, signed */
    check_int("atoiwraphuge", atoi("4294967296"), 0);   /* huge input, deterministic wrap */

    /* atol: 32-bit decimal parse, same rules as atoi but full long range */
    check_long("atol0", atol("0"), 0L);
    check_long("atolpos", atol("123"), 123L);
    check_long("atolneg", atol("-123"), -123L);
    check_long("atolplus", atol("+456"), 456L);
    check_long("atolws", atol("   789"), 789L);
    check_long("atoltab", atol("\t-42"), -42L);
    check_long("atoljunk", atol("123abc"), 123L);
    check_long("atolnodig", atol("abc"), 0L);
    check_long("atolempty", atol(""), 0L);
    check_long("atolwide", atol("65536"), 65536L);
    check_long("atolmillion", atol("1000000"), 1000000L);
    check_long("atolmax", atol("2147483647"), 2147483647L);
    check_long("atolnearmin", atol("-2147483647"), -2147483647L);
    check_long("atolmin", atol("-2147483648"), -2147483647L - 1L);
    check_long("atolnegzero", atol("  -0"), 0L);
    check_long("atolwrap32", atol("4294967296"), 0L);              /* 2^32 wraps to 0 */
    check_long("atolwrap32p1", atol("4294967297"), 1L);            /* 2^32+1 wraps to 1 */
    check_long("atolwrapbig", atol("9999999999"), 1410065407L);    /* 9999999999 mod 2^32, signed */
    check_long("atolwraphuge", atol("99999999999999999999"), 1661992959L); /* deterministic wrap, not UB crash */

    if (failures != 0)
        return 1;

    printf("tstdlib: all tests passed\n");
    return 0;
}
