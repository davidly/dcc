/*
 * tc89c2.c -- test C89 functions added for compliance round 2:
 *   strtod, strxfrm, getenv, system, mblen/mbtowc/wctomb/mbstowcs/wcstombs,
 *   signal/raise, setlocale/localeconv, clock/time/difftime.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <locale.h>
#include <time.h>

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } } while(0)

static void test_strtod(void)
{
    char *end;
    float v;

    v = strtod("3.14", &end);
    CHECK(*end == '\0', "strtod end");
    CHECK(v > 3.13f && v < 3.15f, "strtod value");

    v = strtod("  -2.5e1rest", &end);
    CHECK(v > -25.1f && v < -24.9f, "strtod negative exp");
    CHECK(*end == 'r', "strtod end2");

    v = strtod("0", &end);
    CHECK(v == 0.0f, "strtod zero");
}

static void test_strxfrm(void)
{
    char buf[16];
    size_t n;

    n = strxfrm(buf, "hello", sizeof(buf));
    CHECK(n == 5, "strxfrm len");
    CHECK(strcmp(buf, "hello") == 0, "strxfrm content");

    /* n==0: nothing written, still returns strlen */
    n = strxfrm(buf, "abc", 0);
    CHECK(n == 3, "strxfrm n0 len");

    /* n smaller than src: copies n chars, no null */
    strxfrm(buf, "world", 3);
    CHECK(buf[0] == 'w' && buf[1] == 'o' && buf[2] == 'r', "strxfrm truncate");
}

static void test_getenv_system(void)
{
    CHECK(getenv("PATH") == NULL, "getenv NULL on CP/M");
    CHECK(system(NULL) == 0, "system(NULL)==0");
    CHECK(system("cmd") == -1, "system(cmd)==-1");
}

static void test_multibyte(void)
{
    wchar_t wc;
    char mb[4];
    wchar_t wbuf[8];
    char cbuf[8];
    size_t n;

    CHECK(mblen(NULL, 1) == 0, "mblen NULL");
    CHECK(mblen("A", 1) == 1, "mblen A");
    CHECK(mblen("", 1) == 0, "mblen empty");
    CHECK(mblen("A", 0) == -1, "mblen n0");

    CHECK(mbtowc(&wc, "Z", 1) == 1, "mbtowc ret");
    CHECK(wc == 'Z', "mbtowc val");
    CHECK(mbtowc(NULL, "Z", 1) == 1, "mbtowc null pwc");

    CHECK(wctomb(mb, 'X') == 1, "wctomb ret");
    CHECK(mb[0] == 'X', "wctomb val");
    CHECK(wctomb(NULL, 0) == 0, "wctomb null");

    n = mbstowcs(wbuf, "hi", 4);
    CHECK(n == 2, "mbstowcs len");
    CHECK(wbuf[0] == 'h' && wbuf[1] == 'i' && wbuf[2] == 0, "mbstowcs content");

    n = wcstombs(cbuf, wbuf, sizeof(cbuf));
    CHECK(n == 2, "wcstombs len");
    CHECK(strcmp(cbuf, "hi") == 0, "wcstombs content");
}

static void test_signal(void)
{
    /* signal() always returns SIG_ERR on CP/M */
    CHECK(signal(SIGINT, SIG_DFL) == SIG_ERR, "signal SIG_ERR");
    /* raise of non-SIGABRT signals returns 0 */
    CHECK(raise(SIGTERM) == 0, "raise SIGTERM");
    CHECK(raise(SIGINT)  == 0, "raise SIGINT");
}

static void test_locale(void)
{
    struct lconv *lc;
    char *r;

    r = setlocale(LC_ALL, NULL);
    CHECK(r != NULL && r[0] == 'C' && r[1] == '\0', "setlocale query");

    r = setlocale(LC_ALL, "C");
    CHECK(r != NULL && r[0] == 'C', "setlocale C");

    r = setlocale(LC_ALL, "");
    CHECK(r != NULL && r[0] == 'C', "setlocale empty");

    r = setlocale(LC_ALL, "fr_FR");
    CHECK(r == NULL, "setlocale bad");

    lc = localeconv();
    CHECK(lc != NULL, "localeconv non-null");
    CHECK(lc->decimal_point[0] == '.', "localeconv decimal");
    CHECK(lc->thousands_sep[0] == '\0', "localeconv thousands");
}

static void test_time(void)
{
    time_t t;
    clock_t c;

    c = clock();
    CHECK(c == (clock_t)-1, "clock -1");

    /* time() now reads a real clock via BDOS 105 where the underlying
     * system implements it (see time.h), so its result is environment-
     * dependent: either (time_t)-1 (no clock) or a plausible Unix epoch. */
    t = time(NULL);
    CHECK(t == (time_t)-1 || (t > 1577836800L && t < 2147483647L),
          "time NULL -1 or plausible");

    t = time(&t);
    CHECK(t == (time_t)-1 || (t > 1577836800L && t < 2147483647L),
          "time ptr -1 or plausible");

    CHECK(mktime(NULL) == (time_t)-1, "mktime -1");
    CHECK(asctime(NULL) == NULL, "asctime NULL");
    CHECK(ctime(NULL)   == NULL, "ctime NULL");
    CHECK(gmtime(NULL)  == NULL, "gmtime NULL");
    CHECK(localtime(NULL) == NULL, "localtime NULL");
    CHECK(strftime(NULL, 0, NULL, NULL) == 0, "strftime 0");

    /* difftime: (time_t)-1 - (time_t)-1 = 0 */
    {
        time_t a = (time_t)-1, b = (time_t)-1;
        float d = difftime(a, b);
        CHECK(d == 0.0f, "difftime zero");
    }
}

static void test_huge_val(void)
{
    CHECK(HUGE_VAL > 3.0e38f, "HUGE_VAL large");
}

int main(void)
{
    test_strtod();
    test_strxfrm();
    test_getenv_system();
    test_multibyte();
    test_signal();
    test_locale();
    test_time();
    test_huge_val();

    if (fails == 0)
        printf("tc89c2 ok\n");
    else
        printf("%d failure(s)\n", fails);
    return fails ? 1 : 0;
}
