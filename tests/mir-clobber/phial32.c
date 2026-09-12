#include <stdio.h>

#ifndef PHI_ALIAS_FAULT
#define PHI_ALIAS_FAULT 0
#endif

static int failures;
static unsigned int checks;
static unsigned int oracle_hash = 23117U;

static unsigned int change_word(
    unsigned int *target, unsigned int *alias, unsigned int delta)
{
    *alias = (unsigned int)(*alias + delta);
    return *target;
}

static unsigned int change_byte(
    unsigned char *target, unsigned char *alias, unsigned int delta)
{
    *alias = (unsigned char)(*alias + delta);
    return *target;
}

static unsigned int phi_alias_word(
    unsigned int *target, unsigned int *alias, int choose,
    unsigned int delta)
{
    unsigned int selected;
    unsigned int live_a;
    unsigned int live_b;
    unsigned int live_c;
    unsigned int observed;
    unsigned int result;

    if (choose)
        selected = (unsigned int)(*target + 0x1234U);
    else
        selected = (unsigned int)(*target ^ 0x55aaU);
    live_a = (unsigned int)(selected + 0x0101U);
    live_b = (unsigned int)(selected ^ 0xa55aU);
    live_c = (unsigned int)(selected * 3U);
    observed = change_word(target, alias, delta);
    result = (unsigned int)(
        selected + live_a + live_b + live_c + observed + *target);
    return result ^ PHI_ALIAS_FAULT;
}

static unsigned int phi_alias_byte(
    unsigned char *target, unsigned char *alias, int choose,
    unsigned int delta)
{
    unsigned char selected;
    unsigned int live_a;
    unsigned int live_b;
    unsigned int live_c;
    unsigned int observed;
    unsigned int result;

    if (choose)
        selected = (unsigned char)(*target + 0x34U);
    else
        selected = (unsigned char)(*target ^ 0xaaU);
    live_a = (unsigned int)(selected + 0x0101U);
    live_b = (unsigned int)(selected ^ 0xa55aU);
    live_c = (unsigned int)(selected * 3U);
    observed = change_byte(target, alias, delta);
    result = (unsigned int)(
        selected + live_a + live_b + live_c + observed + *target);
    return result ^ PHI_ALIAS_FAULT;
}

static void check(
    const char *name, unsigned int actual, unsigned int expected,
    unsigned int target, unsigned int other)
{
    ++checks;
    oracle_hash = (unsigned int)(
        oracle_hash * 109U + actual + target * 3U + other * 5U + checks);
    if (actual != expected) {
        printf("FAIL %s got=%u expected=%u target=%u other=%u\n",
               name, actual, expected, target, other);
        ++failures;
    }
}

static void check_words(void)
{
    unsigned int values[2];
    unsigned int actual;

    values[0] = 0x1020U;
    values[1] = 0x3040U;
    actual = phi_alias_word(values, values, 1, 7U);
    check("word-alias-true", actual, 21505U, values[0], values[1]);

    values[0] = 0x1020U;
    values[1] = 0x3040U;
    actual = phi_alias_word(values, values + 1, 0, 9U);
    check("word-disjoint-false", actual, 24003U, values[0], values[1]);

    values[0] = 0xffffU;
    values[1] = 0x2222U;
    actual = phi_alias_word(values, values, 0, 1U);
    check("word-wrap-false", actual, 25529U, values[0], values[1]);

    values[0] = 0U;
    values[1] = 0x8000U;
    actual = phi_alias_word(values, values + 1, 1, 0xffffU);
    check("word-disjoint-true", actual, 4979U, values[0], values[1]);
}

static void check_bytes(void)
{
    unsigned char values[2];
    unsigned int actual;

    values[0] = 0xf0U;
    values[1] = 0x31U;
    actual = phi_alias_byte(values, values, 1, 0x30U);
    check("byte-alias-true", actual, 42867U, values[0], values[1]);

    values[0] = 0x20U;
    values[1] = 0x40U;
    actual = phi_alias_byte(values, values + 1, 0, 9U);
    check("byte-disjoint-false", actual, 43459U, values[0], values[1]);

    values[0] = 0xffU;
    values[1] = 0x22U;
    actual = phi_alias_byte(values, values, 0, 1U);
    check("byte-wrap-false", actual, 42937U, values[0], values[1]);

    values[0] = 0U;
    values[1] = 0x80U;
    actual = phi_alias_byte(values, values + 1, 1, 0xffU);
    check("byte-disjoint-true", actual, 42867U, values[0], values[1]);
}

int main(void)
{
    check_words();
    check_bytes();
    printf("PHIAL32 checks=%u failures=%d hash=%u\n",
           checks, failures, oracle_hash);
    return failures;
}
