/* tcalb11.c - Batch 11 calendar/time boundary and safety regression. */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

extern time_t _tmcv(unsigned cpm_day, unsigned hour,
                    unsigned minute, unsigned second);

static int fails;

static void check(const char *name, int ok)
{
    if (ok)
        printf("PASS %s\n", name);
    else {
        printf("FAIL %s\n", name);
        fails++;
    }
}

static void set_tm(struct tm *t, int year, int mon, int mday,
                   int hour, int min, int sec)
{
    memset(t, 0, sizeof(*t));
    t->tm_year = year;
    t->tm_mon = mon;
    t->tm_mday = mday;
    t->tm_hour = hour;
    t->tm_min = min;
    t->tm_sec = sec;
    t->tm_wday = 1234;
    t->tm_yday = -1234;
    t->tm_isdst = 7;
}

static void check_mktime_fail(const char *name, struct tm *t)
{
    struct tm before;
    time_t got;

    before = *t;
    got = mktime(t);
    check(name, got == (time_t)-1 &&
                memcmp(t, &before, sizeof(*t)) == 0);
}

static void check_float_bits(const char *name, float value,
                             unsigned long want)
{
    union {
        float f;
        unsigned long u;
    } bits;

    bits.f = value;
    check(name, bits.u == want);
}

static void test_time_core(void)
{
    check("CP/M day 0 epoch mapping",
          _tmcv(0, 0, 0, 0) == 252374400L);
    check("CP/M day 1 is 1978-01-01",
          _tmcv(1, 0, 0, 0) == 252460800L);
    check("time core accepts 2038 final second",
          _tmcv(21934, 3, 14, 7) == LONG_MAX);
    check("time core rejects 2038 next second",
          _tmcv(21934, 3, 14, 8) == (time_t)-1);
    check("time core rejects later day",
          _tmcv(21935, 0, 0, 0) == (time_t)-1);
    check("time core rejects 16-bit day wrap pattern",
          _tmcv(65535U, 0, 0, 0) == (time_t)-1);
    check("time core rejects hour 24",
          _tmcv(1, 24, 0, 0) == (time_t)-1);
    check("time core rejects minute 60",
          _tmcv(1, 0, 60, 0) == (time_t)-1);
    check("time core rejects second 60",
          _tmcv(1, 0, 0, 60) == (time_t)-1);
}

static void test_difftime_edges(void)
{
    time_t hi;
    time_t lo;

    check_float_bits("difftime positive overflow",
                     difftime(LONG_MAX, -1L), 0x4f000000UL);
    check_float_bits("difftime positive full span",
                     difftime(LONG_MAX, LONG_MIN), 0x4f800000UL);
    check_float_bits("difftime negative full span",
                     difftime(LONG_MIN, LONG_MAX), 0xcf800000UL);
    check_float_bits("difftime negative boundary",
                     difftime(LONG_MIN, 0L), 0xcf000000UL);
    check("difftime preserves high-value unit delta",
          difftime(LONG_MAX, LONG_MAX - 1L) == 1.0f);

    hi = (time_t)0xffffffffUL;
    lo = (time_t)0x80000000UL;
    check_float_bits("difftime signed bit patterns forward",
                     difftime(hi, lo), 0x4f000000UL);
    check_float_bits("difftime signed bit patterns reverse",
                     difftime(lo, hi), 0xcf000000UL);
}

static void test_mktime_normalization(void)
{
    struct tm t;
    time_t got;

    set_tm(&t, 69, 11, 32, 0, 0, 0);
    got = mktime(&t);
    check("mktime day crosses 1970 before range check",
          got == 0L && t.tm_year == 70 && t.tm_mon == 0 &&
          t.tm_mday == 1);

    set_tm(&t, 69, 12, 1, 0, 0, 0);
    got = mktime(&t);
    check("mktime month crosses 1970",
          got == 0L && t.tm_year == 70 && t.tm_mon == 0 &&
          t.tm_mday == 1);

    set_tm(&t, 70, -1, 32, 0, 0, 0);
    got = mktime(&t);
    check("mktime negative month plus day normalizes",
          got == 0L && t.tm_year == 70 && t.tm_mon == 0 &&
          t.tm_mday == 1);

    set_tm(&t, 70, 0, 2, 0, 0, -1);
    got = mktime(&t);
    check("mktime negative second borrows",
          got == 86399L && t.tm_hour == 23 && t.tm_min == 59 &&
          t.tm_sec == 59 && t.tm_mday == 1);

    set_tm(&t, 70, 0, 1, 23, 59, 60);
    got = mktime(&t);
    check("mktime positive second carries",
          got == 86400L && t.tm_hour == 0 && t.tm_min == 0 &&
          t.tm_sec == 0 && t.tm_mday == 2);

    set_tm(&t, 70, 0, 1, 32767, 0, 0);
    got = mktime(&t);
    check("mktime large hour normalizes without overflow",
          got == 117961200L && t.tm_year == 73 && t.tm_mon == 8 &&
          t.tm_mday == 27 && t.tm_hour == 7);

    set_tm(&t, 70, 1, 0, 0, 0, 0);
    got = mktime(&t);
    check("mktime day zero borrows month",
          got == 2592000L && t.tm_mon == 0 && t.tm_mday == 31);

    set_tm(&t, 100, 1, 29, 0, 0, 0);
    got = mktime(&t);
    check("mktime accepts 2000 leap day",
          got == 951782400L && t.tm_mon == 1 && t.tm_mday == 29);

    set_tm(&t, 101, 1, 29, 0, 0, 0);
    got = mktime(&t);
    check("mktime normalizes non-leap February",
          got == 983404800L && t.tm_mon == 2 && t.tm_mday == 1);

    set_tm(&t, 138, 0, 19, 3, 14, 7);
    got = mktime(&t);
    check("mktime accepts signed time_t maximum",
          got == LONG_MAX && t.tm_year == 138 && t.tm_yday == 18);

    set_tm(&t, 138, 0, 19, 3, 14, 8);
    check_mktime_fail("mktime rejects 2038 first unrepresentable second", &t);

    set_tm(&t, 206, 1, 7, 6, 28, 15);
    check_mktime_fail("mktime rejects unsigned 32-bit maximum pattern", &t);

    set_tm(&t, 206, 1, 7, 6, 28, 16);
    check_mktime_fail("mktime rejects unsigned 32-bit wrap second", &t);

    set_tm(&t, 206, 1, 8, 0, 0, 0);
    check_mktime_fail("mktime rejects post-2106 wrapped date", &t);

    set_tm(&t, 70, 0, 1, 0, 0, -1);
    check_mktime_fail("mktime rejects pre-epoch normalized value", &t);

    set_tm(&t, 32767, -32768, 32767, -32768, 32767, -32768);
    check_mktime_fail("mktime rejects extreme fields without mutation", &t);
}

static void test_gmtime_bit_patterns(void)
{
    time_t t;

    t = (time_t)0x7fffffffUL;
    check("gmtime accepts positive maximum bit pattern", gmtime(&t) != NULL);
    t = (time_t)0x80000000UL;
    check("gmtime rejects sign-bit time_t pattern", gmtime(&t) == NULL);
    t = (time_t)0xffffffffUL;
    check("gmtime rejects all-ones time_t pattern", gmtime(&t) == NULL);
}

static void test_mktime_preserves_gmtime(void)
{
    time_t source;
    time_t got;
    struct tm *shared;
    struct tm snapshot;
    struct tm other;

    source = 946684800L;
    shared = gmtime(&source);
    if (shared == NULL) {
        check("mktime preserves prior gmtime result", 0);
        return;
    }
    snapshot = *shared;
    set_tm(&other, 126, 7, 17, 12, 34, 56);
    got = mktime(&other);
    check("mktime preserves prior gmtime result",
          got == 1786970096L &&
          memcmp(shared, &snapshot, sizeof(snapshot)) == 0);
}

static void check_asctime_fail(const char *name, struct tm *t,
                               char *buf, const char *sentinel)
{
    check(name, asctime(t) == NULL && memcmp(buf, sentinel, 26) == 0);
}

static void test_asctime_safety(void)
{
    struct tm good;
    struct tm bad;
    char sentinel[26];
    char *buf;

    set_tm(&good, 138, 0, 19, 3, 14, 7);
    good.tm_wday = 2;
    buf = asctime(&good);
    check("asctime valid fixed layout",
          buf != NULL && strcmp(buf, "Tue Jan 19 03:14:07 2038\n") == 0 &&
          strlen(buf) == 25);

    set_tm(&good, 8099, 11, 31, 23, 59, 60);
    good.tm_wday = 0;
    check("asctime bounded year 9999 and leap second",
          strcmp(asctime(&good), "Sun Dec 31 23:59:60 9999\n") == 0);

    memset(buf, 'Z', 26);
    memcpy(sentinel, buf, 26);

    bad = good; bad.tm_wday = -1;
    check_asctime_fail("asctime rejects negative weekday", &bad, buf, sentinel);
    bad = good; bad.tm_wday = 7;
    check_asctime_fail("asctime rejects weekday 7", &bad, buf, sentinel);
    bad = good; bad.tm_mon = -1;
    check_asctime_fail("asctime rejects negative month", &bad, buf, sentinel);
    bad = good; bad.tm_mon = 12;
    check_asctime_fail("asctime rejects month 12", &bad, buf, sentinel);
    bad = good; bad.tm_mday = 0;
    check_asctime_fail("asctime rejects day zero", &bad, buf, sentinel);
    bad = good; bad.tm_mday = 32;
    check_asctime_fail("asctime rejects day 32", &bad, buf, sentinel);
    bad = good; bad.tm_hour = 24;
    check_asctime_fail("asctime rejects hour 24", &bad, buf, sentinel);
    bad = good; bad.tm_min = 60;
    check_asctime_fail("asctime rejects minute 60", &bad, buf, sentinel);
    bad = good; bad.tm_sec = 61;
    check_asctime_fail("asctime rejects second 61", &bad, buf, sentinel);
    bad = good; bad.tm_year = 8100;
    check_asctime_fail("asctime rejects five-digit year", &bad, buf, sentinel);
    bad = good; bad.tm_year = -1901;
    check_asctime_fail("asctime rejects negative actual year", &bad, buf, sentinel);
}

int main(void)
{
    test_time_core();
    test_difftime_edges();
    test_mktime_normalization();
    test_gmtime_bit_patterns();
    test_mktime_preserves_gmtime();
    test_asctime_safety();

    if (fails) {
        printf("tcalb11 failed: %d\n", fails);
        return 1;
    }
    printf("tcalb11 passed\n");
    return 0;
}
