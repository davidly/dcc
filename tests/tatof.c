#include <stdio.h>
#include <stdlib.h>

static int failures;
static int checks;

static void chki(const char *name, int got, int expected)
{
    checks++;
    if (got != expected) {
        printf("FAIL %s: got %d expected %d\n", name, got, expected);
        failures++;
    }
}

static void chkf(const char *name, float got, float expected, float tolerance)
{
    float diff;
    checks++;
    diff = got - expected;
    if (diff < 0.0f)
        diff = -diff;
    if (diff > tolerance) {
        printf("FAIL %s: got %f expected %f\n", name, got, expected);
        failures++;
    }
}

static void chk_end(const char *name, const char *text, int expected)
{
    char *end;
    (void)strtod(text, &end);
    chki(name, *end, expected);
}

static void chk_inf(const char *name, float value, int negative)
{
    union fb { float f; unsigned char b[4]; } bits;
    checks++;
    bits.f = value;
    if (bits.b[0] != 0 || bits.b[1] != 0 || bits.b[2] != 0x80 ||
        bits.b[3] != (unsigned char)(negative ? 0xff : 0x7f)) {
        printf("FAIL %s: not infinity\n", name);
        failures++;
    }
}

static void chk_nan(const char *name, float value)
{
    union fb { float f; unsigned char b[4]; } bits;
    checks++;
    bits.f = value;
    if (value == value || (bits.b[3] & 0x7f) != 0x7f ||
        (bits.b[2] & 0x80) == 0 ||
        ((bits.b[2] & 0x7f) == 0 && bits.b[1] == 0 && bits.b[0] == 0)) {
        printf("FAIL %s: not NaN\n", name);
        failures++;
    }
}

int main(void)
{
    chkf("zero", atof("0"), 0.0f, 0.0f);
    chkf("pos_one", atof("1"), 1.0f, 0.0f);
    chkf("neg_one", atof("-1"), -1.0f, 0.0f);
    chkf("frac", atof("3.5"), 3.5f, 0.0f);
    chkf("neg_frac", atof("-2.5"), -2.5f, 0.0f);
    chkf("leading_dot", atof(".25"), 0.25f, 0.0f);
    chkf("trailing_dot", atof("5."), 5.0f, 0.0f);
    chkf("ws", atof(" \t\n\r+3.5"), 3.5f, 0.0f);
    chkf("exp_pos", atof("1.5e2"), 150.0f, 0.0f);
    chkf("exp_neg", atof("5e-1"), 0.5f, 0.0001f);
    chkf("exp_plus", atof("+0.5e+1"), 5.0f, 0.0f);
    chkf("junk", atof("12.25xyz"), 12.25f, 0.0f);
    chkf("empty", atof(""), 0.0f, 0.0f);
    chkf("nodig", atof("abc"), 0.0f, 0.0f);
    chkf("bare_exp", atof("12e"), 12.0f, 0.0f);
    chkf("bad_exp_sign", atof("12e+x"), 12.0f, 0.0f);

    chki("int_cast", (int)atof("42"), 42);
    chki("neg_int_cast", (int)atof("-17"), -17);
    chki("frac_scale", (int)(atof("3.5") * 2.0f), 7);

    chk_end("end_junk", "12.25xyz", 'x');
    chk_end("end_bare_exp", "12e", 'e');
    chk_end("end_bad_exp_sign", "12e+x", 'e');
    chk_end("end_inf_tail", "infinitive", 'i');
    chk_end("end_infinity", "infinity!", '!');

    chk_inf("inf", atof("inf"), 0);
    chk_inf("infinity", atof("INFINITY"), 0);
    chk_inf("neg_inf", atof("-inf"), 1);
    chk_inf("overflow", atof("1e9999"), 0);
    chk_inf("neg_overflow", atof("-1e9999"), 1);
    chkf("underflow", atof("1e-9999"), 0.0f, 0.0f);
    chkf("neg_underflow", atof("-1e-9999"), 0.0f, 0.0f);
    chkf("zero_huge_exp", atof("0e9999"), 0.0f, 0.0f);
    chk_nan("nan", atof("nan"));
    chk_nan("nan_case", atof("NaN"));
    chk_nan("neg_nan", atof("-nan"));

    /* Small-but-representable value: many significant digits with a large
     * negative exponent must not be flushed to zero.  1.23456789e-38 is a
     * normal float (> FLT_MIN), so scaling it back up recovers ~1.2345679. */
    chkf("small_norm", atof("123456789e-46") * 1e38f, 1.2345679f, 0.01f);
    chkf("near_fltmin", atof("2e-38") * 1e38f, 2.0f, 0.01f);
    chkf("long39", atof("123456789012345678901234567890123456789e-38"), 1.2345679f, 0.01f);
    chkf("long40", atof("1234567890123456789012345678901234567890e-39"), 1.2345679f, 0.01f);
    chkf("long_frac", atof("1.234567890123456789012345678901234567890"), 1.2345679f, 0.01f);

    /* strtod endptr: a token with no mantissa digit is "no conversion",
     * so endptr must stay at the original nptr. */
    chk_end("end_e_no_mant", "e5", 'e');
    chk_end("end_dot_only", ".", '.');
    chk_end("end_dot_e", ".e5", '.');
    chk_end("end_plus_dot", "+.", '+');
    chkf("val_e_no_mant", atof("e5"), 0.0f, 0.0f);
    chkf("val_dot_only", atof("."), 0.0f, 0.0f);

    if (failures)
        printf("tatof FAILED %d\n", failures);
    else
        printf("tatof ok %d checks\n", checks);
    return failures ? 1 : 0;
}
