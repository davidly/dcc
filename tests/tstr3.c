/*
 * tstr3.c -- regression tests for remaining C89 string funcs.
 *
 * Covers:
 *   strcoll
 *   strcspn
 *   strpbrk
 *   strspn
 *   strdup
 *
 * Expected:
 *   trtl_string_remaining: all tests passed
 */

#ifdef _MSC_VER
#define _CRT_NONSTDC_NO_DEPRECATE
#define strdup _strdup
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void fail_i(const char *name, int got, int expected)
{
    printf("FAIL %s got %d expected %d\n", name, got, expected);
    failures++;
}

static void fail_s(const char *name, const char *got, const char *expected)
{
    printf("FAIL %s got '%s' expected '%s'\n", name, got, expected);
    failures++;
}

static void fail_msg(const char *name)
{
    printf("FAIL %s\n", name);
    failures++;
}

static void check_i(const char *name, int got, int expected)
{
    if (got != expected)
        fail_i(name, got, expected);
}

static void check_s(const char *name, const char *got, const char *expected)
{
    if (strcmp(got, expected) != 0)
        fail_s(name, got, expected);
}

static void test_strcoll(void)
{
    check_i("strcoll equal", strcoll("abc", "abc"), 0);

    if (strcoll("abc", "abd") >= 0)
        fail_msg("strcoll less");

    if (strcoll("abd", "abc") <= 0)
        fail_msg("strcoll greater");

    if (strcoll("", "a") >= 0)
        fail_msg("strcoll empty less");
}

static void test_strcspn(void)
{
    check_i("strcspn basic", (int)strcspn("abcdef", "dx"), 3);
    check_i("strcspn none", (int)strcspn("abcdef", "xyz"), 6);
    check_i("strcspn first", (int)strcspn("abcdef", "a"), 0);
    check_i("strcspn empty reject", (int)strcspn("abc", ""), 3);
    check_i("strcspn empty s", (int)strcspn("", "abc"), 0);
}

static void test_strpbrk(void)
{
    char s[] = "abcdef";
    char *p;

    p = strpbrk(s, "xzde");
    if (p != s + 3)
        fail_msg("strpbrk basic");
    else
        check_s("strpbrk suffix", p, "def");

    p = strpbrk(s, "xyz");
    if (p != 0)
        fail_msg("strpbrk missing");

    p = strpbrk(s, "");
    if (p != 0)
        fail_msg("strpbrk empty accept");

    p = strpbrk("", "abc");
    if (p != 0)
        fail_msg("strpbrk empty s");
}

static void test_strspn(void)
{
    check_i("strspn basic", (int)strspn("abcde", "abc"), 3);
    check_i("strspn all", (int)strspn("aaaa", "a"), 4);
    check_i("strspn none", (int)strspn("abc", "xyz"), 0);
    check_i("strspn empty accept", (int)strspn("abc", ""), 0);
    check_i("strspn empty s", (int)strspn("", "abc"), 0);
}

static void test_strdup(void)
{
    char src[16];
    char *p;

    strcpy(src, "hello");
    p = strdup(src);

    if (p == 0) {
        fail_msg("strdup nonnull");
        return;
    }

    check_s("strdup contents", p, "hello");

    src[0] = 'j';
    check_s("strdup independent", p, "hello");

    p[1] = 'a';
    check_s("strdup writable", p, "hallo");

    free(p);

    p = strdup("");
    if (p == 0) {
        fail_msg("strdup empty nonnull");
        return;
    }

    check_s("strdup empty", p, "");
    free(p);

    /* Long string: catches a strlen-argument-passing bug in __sdup where
     * a garbage length let a short string appear to "work" by luck. */
    p = strdup("this is a somewhat longer test string for strdup verification 1234567890");
    if (p == 0) {
        fail_msg("strdup long nonnull");
        return;
    }
    check_i("strdup long length", (int)strlen(p), 72);
    check_s("strdup long contents", p,
        "this is a somewhat longer test string for strdup verification 1234567890");
    free(p);
}

int main(void)
{
    test_strcoll();
    test_strcspn();
    test_strpbrk();
    test_strspn();
    test_strdup();

    if (failures) {
        printf("trtl_string_remaining: %d failure(s)\n", failures);
        return 1;
    }

    printf("tstr3 completed with great success\n" );
    return 0;
}
