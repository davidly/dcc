#include <stdio.h>
#include <string.h>
#include <time.h>

static int fails;
static struct tm tmv;

static void fail(const char *msg)
{
    printf("FAIL %s\n", msg);
    fails++;
}

static void init_time(void)
{
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_sec = 56;
    tmv.tm_min = 34;
    tmv.tm_hour = 12;
    tmv.tm_mday = 17;
    tmv.tm_mon = 7;
    tmv.tm_year = 126;
    tmv.tm_wday = 1;
    tmv.tm_yday = 228;
}

static void check_fmt(const char *fmt, const char *want)
{
    unsigned char raw[130];
    char *buf;
    size_t n;

    memset(raw, 0x5a, sizeof(raw));
    buf = (char *)&raw[1];
    n = strftime(buf, 128, fmt, &tmv);
    if (n != strlen(want) || strcmp(buf, want) != 0)
        fail(fmt);
    if (raw[0] != 0x5a || raw[129] != 0x5a)
        fail("format sentinel");
}

static void test_specifiers(void)
{
    static const char *fmts[] = {
        "%a", "%A", "%b", "%B", "%C", "%c", "%d", "%H", "%I",
        "%j", "%m", "%M", "%p", "%S", "%U", "%w", "%W",
        "%x", "%X", "%y", "%Y", "%Z", "%%"
    };
    static const char *wants[] = {
        "Mon", "Monday", "Aug", "August", "20",
        "Mon Aug 17 12:34:56 2026",
        "17", "12", "12", "229", "08", "34", "PM", "56",
        "33", "1", "33", "08/17/26", "12:34:56", "26",
        "2026", "", "%"
    };
    int i;

    for (i = 0; i < (int)(sizeof(fmts) / sizeof(fmts[0])); ++i)
        check_fmt(fmts[i], wants[i]);
    check_fmt("literal %a %% %Z end", "literal Mon %  end");
    check_fmt("", "");
}

static void test_names(void)
{
    static const char *days[] = {
        "Sun|Sunday", "Mon|Monday", "Tue|Tuesday", "Wed|Wednesday",
        "Thu|Thursday", "Fri|Friday", "Sat|Saturday"
    };
    static const char *months[] = {
        "Jan|January", "Feb|February", "Mar|March", "Apr|April",
        "May|May", "Jun|June", "Jul|July", "Aug|August",
        "Sep|September", "Oct|October", "Nov|November", "Dec|December"
    };
    int i;

    for (i = 0; i < 7; ++i) {
        tmv.tm_wday = i;
        check_fmt("%a|%A", days[i]);
    }
    tmv.tm_wday = 1;
    for (i = 0; i < 12; ++i) {
        tmv.tm_mon = i;
        check_fmt("%b|%B", months[i]);
    }
    tmv.tm_mon = 7;
}

static void test_numeric_edges(void)
{
    init_time();
    tmv.tm_sec = 0;
    tmv.tm_min = 0;
    tmv.tm_hour = 0;
    tmv.tm_mday = 1;
    tmv.tm_mon = 0;
    tmv.tm_year = 70;
    tmv.tm_wday = 4;
    tmv.tm_yday = 0;
    check_fmt("%d %H %I %j %m %M %p %S %U %w %W %x %X %y %Y",
              "01 00 12 001 01 00 AM 00 00 4 00 01/01/70 00:00:00 70 1970");

    tmv.tm_sec = 60;
    tmv.tm_min = 59;
    tmv.tm_hour = 23;
    tmv.tm_mday = 31;
    tmv.tm_mon = 11;
    tmv.tm_year = 138;
    tmv.tm_wday = 6;
    tmv.tm_yday = 365;
    check_fmt("%d %H %I %j %m %M %p %S %U %w %W %x %X %y %Y",
              "31 23 11 366 12 59 PM 60 52 6 52 12/31/38 23:59:60 38 2038");

    tmv.tm_wday = 0;
    check_fmt("%U|%W", "53|52");
    tmv.tm_wday = 1;
    check_fmt("%U|%W", "53|53");

    tmv.tm_hour = 11;
    check_fmt("%I|%p", "11|AM");
    tmv.tm_hour = 12;
    check_fmt("%I|%p", "12|PM");

    tmv.tm_year = -1900;
    check_fmt("%C|%Y|%y", "00|0000|00");
    tmv.tm_year = 8099;
    check_fmt("%C|%Y|%y", "99|9999|99");
    tmv.tm_year = 8100;
    check_fmt("%C|%Y|%y", "100|10000|00");
    tmv.tm_year = 32767;
    check_fmt("%C|%Y|%y", "346|34667|67");

    init_time();
    tmv.tm_sec = 3;
    tmv.tm_min = 2;
    tmv.tm_hour = 1;
    tmv.tm_mday = 3;
    tmv.tm_mon = 0;
    tmv.tm_year = 101;
    tmv.tm_wday = 0;
    tmv.tm_yday = 2;
    check_fmt("%c", "Sun Jan  3 01:02:03 2001");

    tmv.tm_mon = 1;
    tmv.tm_mday = 31;
    check_fmt("%m-%d", "02-31");

    tmv.tm_isdst = -32767 - 1;
    check_fmt("%Y", "2001");
    tmv.tm_isdst = 32767;
    check_fmt("%Y", "2001");
}

static void check_bound(size_t max, const char *fmt, size_t want_n,
                        const char *want)
{
    unsigned char raw[16];
    char *buf;
    size_t n;

    memset(raw, 0x5a, sizeof(raw));
    buf = (char *)&raw[1];
    n = strftime(buf, max, fmt, &tmv);
    if (n != want_n || strcmp(buf, want) != 0)
        fail("boundary result");
    if (raw[0] != 0x5a || raw[max + 1] != 0x5a)
        fail("boundary sentinel");
}

static void test_bounds(void)
{
    unsigned char raw[8];
    size_t n;
    int i;

    init_time();
    check_bound(4, "abc", 3, "abc");
    check_bound(3, "abc", 0, "ab");
    check_bound(2, "abc", 0, "a");
    check_bound(1, "abc", 0, "");
    check_bound(1, "", 0, "");

    tmv.tm_year = -1900;
    check_bound(3, "%C", 2, "00");
    check_bound(2, "%C", 0, "0");
    tmv.tm_year = 8099;
    check_bound(3, "%C", 2, "99");
    check_bound(2, "%C", 0, "9");
    tmv.tm_year = 8100;
    check_bound(4, "%C", 3, "100");
    check_bound(3, "%C", 0, "10");
    tmv.tm_year = 32767;
    check_bound(4, "%C", 3, "346");
    check_bound(3, "%C", 0, "34");

    memset(raw, 0x5a, sizeof(raw));
    n = strftime((char *)&raw[1], 0, NULL, NULL);
    if (n != 0)
        fail("max zero return");
    for (i = 0; i < (int)sizeof(raw); ++i) {
        if (raw[i] != 0x5a)
            fail("max zero write");
    }
    if (strftime(NULL, 0, NULL, NULL) != 0)
        fail("null max zero");
    if (strftime(NULL, 1, "", &tmv) != 0)
        fail("null destination");

    memset(raw, 0x5a, sizeof(raw));
    if (strftime((char *)&raw[1], 6, NULL, &tmv) != 0 ||
        raw[1] != 0 || raw[0] != 0x5a || raw[7] != 0x5a)
        fail("null format");
    memset(raw, 0x5a, sizeof(raw));
    if (strftime((char *)&raw[1], 6, "", NULL) != 0 ||
        raw[1] != 0 || raw[0] != 0x5a || raw[7] != 0x5a)
        fail("null time");
}

static void check_bad_format(const char *fmt, const char *prefix)
{
    unsigned char raw[16];
    char *buf;

    memset(raw, 0x5a, sizeof(raw));
    buf = (char *)&raw[1];
    if (strftime(buf, 14, fmt, &tmv) != 0 || strcmp(buf, prefix) != 0)
        fail("malformed format");
    if (raw[0] != 0x5a || raw[15] != 0x5a)
        fail("malformed sentinel");
}

static void test_malformed(void)
{
    init_time();
    check_bad_format("%", "");
    check_bad_format("ab%", "ab");
    check_bad_format("%Q", "");
    check_bad_format("ab%Qcd", "ab");
    check_bad_format("%e", "");
}

static void check_bad_tm(void)
{
    unsigned char raw[10];
    char *buf;

    memset(raw, 0x5a, sizeof(raw));
    buf = (char *)&raw[1];
    if (strftime(buf, 8, "%Y", &tmv) != 0 || buf[0] != '\0')
        fail("invalid tm result");
    if (raw[0] != 0x5a || raw[9] != 0x5a)
        fail("invalid tm sentinel");
}

static void test_invalid_tm(void)
{
    init_time(); tmv.tm_sec = -1; check_bad_tm();
    init_time(); tmv.tm_sec = 61; check_bad_tm();
    init_time(); tmv.tm_min = -1; check_bad_tm();
    init_time(); tmv.tm_min = 60; check_bad_tm();
    init_time(); tmv.tm_hour = -1; check_bad_tm();
    init_time(); tmv.tm_hour = 24; check_bad_tm();
    init_time(); tmv.tm_mday = 0; check_bad_tm();
    init_time(); tmv.tm_mday = 32; check_bad_tm();
    init_time(); tmv.tm_mon = -1; check_bad_tm();
    init_time(); tmv.tm_mon = 12; check_bad_tm();
    init_time(); tmv.tm_year = -1901; check_bad_tm();
    init_time(); tmv.tm_wday = -1; check_bad_tm();
    init_time(); tmv.tm_wday = 7; check_bad_tm();
    init_time(); tmv.tm_yday = -1; check_bad_tm();
    init_time(); tmv.tm_yday = 366; check_bad_tm();
}

int main(void)
{
    init_time();
    test_specifiers();
    test_names();
    test_numeric_edges();
    test_bounds();
    test_malformed();
    test_invalid_tm();

    if (fails != 0) {
        printf("tstrftm FAILED: %d\n", fails);
        return 1;
    }

    printf("tstrftm: all tests passed\n");
    return 0;
}
