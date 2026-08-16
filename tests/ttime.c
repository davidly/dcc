/* ttime.c - regression coverage for the BDOS-105-backed time()/difftime()
 * and the gmtime/localtime/asctime/ctime/mktime calendar functions built on
 * top of it. Output is PASS/FAIL text only (never the raw clock value), so
 * the baseline stays fixed regardless of when the suite runs or which
 * BDOS/emulator combination it's built for - a system with no working clock
 * (real CP/M 2.2 hardware, or any BDOS that no-ops the call) should report
 * time() as (time_t)-1, exactly as this function did before BDOS 105
 * support existed; a system with a real clock should report a plausible,
 * self-consistent value instead. Both outcomes are checked without
 * hardcoding which one to expect, since that's a property of the runtime
 * the test executes under, not of the RTL implementation itself.
 *
 * Each area of coverage below lives in its own function partly for
 * readability, but mainly because this file is a known trigger for a real
 * dccpeep miscompilation: with peephole optimization on, one of the mktime()
 * normalization checks comes back wrong, and splitting the code into
 * separate functions (tried first) did NOT avoid it - the corruption
 * persists across function-call boundaries, not just within one function's
 * own code shape. tests/_test_overrides.json therefore forces this app to
 * build with dcc-peep=false ("ttime": "dcc_args": "dcc-peep=false").
 *
 * Root cause: src/dccpeep/peep_pass_once.c's try_subtract_one_at() - see
 * the KNOWN BUG comment on that function for the full diagnosis. Already
 * fixed on the perf/unified-regalloc branch (confirmed empirically); once
 * that branch merges, drop the dcc-peep=false override above and confirm
 * this file passes with peephole enabled again. This affects any program
 * built normally (peephole on, the default), not just this test.
 *
 * Host validation of this whole file is skipped (tests/_test_overrides.json's
 * "host": true on "ttime"): dcc's RTL gmtime()/localtime()/asctime()/ctime()
 * deliberately null-check their argument and return NULL for a NULL time_t*,
 * a defensive behavior this file tests directly (see the "returns NULL"
 * checks below) - but passing NULL to those functions is undefined behavior
 * per the C standard, and neither glibc nor Apple's libc null-check it; both
 * just dereference it and segfault. Not a dcc bug, just a defensive
 * guarantee the C standard doesn't require a host libc to offer. */
#include <stdio.h>
#include <string.h>
#include <time.h>

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

static void checkstr(const char *name, const char *got, const char *want)
{
    check(name, got != NULL && strcmp(got, want) == 0);
}

static void test_time(void)
{
    time_t t1, t2, tstored;
    int have_clock;

    t1 = time(NULL);
    have_clock = (t1 != (time_t)-1);
    check("time(NULL) returns -1 or a plausible epoch",
          !have_clock || (t1 > 1577836800L && t1 < 2147483647L));

    if (have_clock) {
        time(&t2);
        check("time(&tp) matches time(NULL) within a few seconds",
              t2 >= t1 && t2 - t1 < 5);

        tstored = 0;
        t1 = time(&tstored);
        check("time(&tp) stores the same value it returns", tstored == t1);
    } else {
        tstored = 12345;
        t2 = time(&tstored);
        check("time(&tp) also returns -1 when unavailable", t2 == (time_t)-1);
        check("time(&tp) still stores -1 through tp when unavailable",
              tstored == (time_t)-1);
    }
}

static void test_difftime(void)
{
    /* difftime() is pure arithmetic on caller-supplied values, so it's
     * fully deterministic regardless of whether a real clock is present. */
    check("difftime(100000,0) == 100000", difftime(100000L, 0L) == 100000.0f);
    check("difftime(0,100000) == -100000", difftime(0L, 100000L) == -100000.0f);
    check("difftime(5,0) == 5 (t1/t0 argument order)", difftime(5L, 0L) == 5.0f);
    check("difftime(0,5) == -5 (t1/t0 argument order)", difftime(0L, 5L) == -5.0f);
    check("difftime across a 65536s boundary == 65536",
          difftime(65541L, 5L) == 65536.0f);
    check("difftime(t,t) == 0", difftime(12345L, 12345L) == 0.0f);
}

static void test_calendar_breakdown(void)
{
    time_t t;
    struct tm *tp;

    /* gmtime/localtime/asctime/ctime/mktime are pure calendar arithmetic on
     * caller-supplied values (this target has no timezone database, so
     * localtime() is just gmtime() under another name), fully deterministic
     * regardless of whether a real clock is present. Reference values below
     * were cross-checked against `date -u -d @<t>` on the host. */

    /* 1970-01-01 00:00:00 was a Thursday. */
    t = 0;
    tp = gmtime(&t);
    check("gmtime NULL check", tp != NULL);
    check("1970-01-01 wday", tp->tm_wday == 4);
    check("1970-01-01 yday", tp->tm_yday == 0);
    check("1970-01-01 mday", tp->tm_mday == 1);
    check("1970-01-01 mon", tp->tm_mon == 0);
    check("1970-01-01 year", tp->tm_year == 70);
    checkstr("1970-01-01 asctime", asctime(tp), "Thu Jan  1 00:00:00 1970\n");

    /* 2000-01-01 00:00:00 was a Saturday. */
    t = 946684800L;
    tp = gmtime(&t);
    check("2000-01-01 wday", tp->tm_wday == 6);
    checkstr("2000-01-01 asctime", asctime(tp), "Sat Jan  1 00:00:00 2000\n");

    /* 2000-02-29 exists: year 2000 is a leap year despite %100==0,
     * because %400==0 - exercises the full leap-year rule, not just
     * the common %4==0 case. */
    t = 951782400L;
    tp = gmtime(&t);
    check("2000-02-29 mday", tp->tm_mday == 29);
    check("2000-02-29 mon", tp->tm_mon == 1);

    /* 32-bit signed time_t max: 2038-01-19 03:14:07 UTC. */
    t = 2147483647L;
    tp = gmtime(&t);
    check("2038 rollover year", tp->tm_year == 138);
    check("2038 rollover mon", tp->tm_mon == 0);
    check("2038 rollover mday", tp->tm_mday == 19);
    check("2038 rollover hour", tp->tm_hour == 3);
    check("2038 rollover min", tp->tm_min == 14);
    check("2038 rollover sec", tp->tm_sec == 7);

    /* No timezone database on this target: localtime() == gmtime(). */
    t = 946684800L;
    tp = localtime(&t);
    check("localtime matches gmtime (no tz database)",
          tp->tm_year == 100 && tp->tm_mon == 0 && tp->tm_mday == 1 &&
          tp->tm_wday == 6);

    /* ctime(tp) == asctime(localtime(tp)) - C89's own definition. */
    checkstr("ctime matches asctime(localtime)", ctime(&t), asctime(localtime(&t)));

    /* NULL handling. */
    check("gmtime(NULL) returns NULL", gmtime(NULL) == NULL);
    check("localtime(NULL) returns NULL", localtime(NULL) == NULL);
    check("asctime(NULL) returns NULL", asctime(NULL) == NULL);
    check("ctime(NULL) returns NULL", ctime(NULL) == NULL);
    check("mktime(NULL) returns -1", mktime(NULL) == (time_t)-1);
}

static void test_mktime_roundtrip(void)
{
    time_t t;
    time_t got;
    struct tm *tp;
    struct tm mt;
    int wday_before;

    /* mktime round-trip: gmtime() then mktime() reproduces the same t
     * and fills in tm_wday/tm_yday even though they weren't set. */
    t = 1234567890L;
    tp = gmtime(&t);
    mt = *tp;
    wday_before = tp->tm_wday;
    got = mktime(&mt);
    check("mktime round-trip", got == t);
    check("mktime round-trip wday filled", mt.tm_wday == wday_before);
}

static void test_mktime_mday_overflow(void)
{
    struct tm mt;
    int mon, mday;

    /* mktime normalization: mday overflow rolls into the next month. */
    memset(&mt, 0, sizeof(mt));
    mt.tm_year = 100; /* 2000 */
    mt.tm_mon = 0;    /* January */
    mt.tm_mday = 32;  /* -> Feb 1 */
    mktime(&mt);
    mon = mt.tm_mon;
    mday = mt.tm_mday;
    check("mktime mday overflow -> mon", mon == 1);
    check("mktime mday overflow -> mday", mday == 1);
}

static void test_mktime_mon_overflow(void)
{
    struct tm mt;
    int year, mon;

    /* mktime normalization: mon==12 ("13th month") rolls into next year. */
    memset(&mt, 0, sizeof(mt));
    mt.tm_year = 100;
    mt.tm_mon = 12;
    mt.tm_mday = 1;
    mktime(&mt);
    year = mt.tm_year;
    mon = mt.tm_mon;
    check("mktime mon==12 -> year", year == 101);
    check("mktime mon==12 -> mon", mon == 0);
}

int main(void)
{
    fails = 0;

    test_time();
    test_difftime();
    test_calendar_breakdown();
    test_mktime_roundtrip();
    test_mktime_mday_overflow();
    test_mktime_mon_overflow();

    if (fails) {
        printf("ttime failed: %d\n", fails);
        return 1;
    }
    printf("ttime passed\n");
    return 0;
}
