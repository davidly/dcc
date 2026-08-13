/* ttime.c - regression coverage for the BDOS-105-backed time()/difftime().
 * Output is PASS/FAIL text only (never the raw clock value), so the baseline
 * stays fixed regardless of when the suite runs or which BDOS/emulator
 * combination it's built for - a system with no working clock (real CP/M
 * 2.2 hardware, or any BDOS that no-ops the call) should report time() as
 * (time_t)-1, exactly as this function did before BDOS 105 support existed;
 * a system with a real clock should report a plausible, self-consistent
 * value instead. Both outcomes are checked without hardcoding which one to
 * expect, since that's a property of the runtime the test executes under,
 * not of the RTL implementation itself. */
#include <stdio.h>
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

int main(void)
{
    time_t t1, t2, tstored;
    int have_clock;

    fails = 0;

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

    /* difftime() is pure arithmetic on caller-supplied values, so it's
     * fully deterministic regardless of whether a real clock is present. */
    check("difftime(100000,0) == 100000", difftime(100000L, 0L) == 100000.0f);
    check("difftime(0,100000) == -100000", difftime(0L, 100000L) == -100000.0f);
    check("difftime(5,0) == 5 (t1/t0 argument order)", difftime(5L, 0L) == 5.0f);
    check("difftime(0,5) == -5 (t1/t0 argument order)", difftime(0L, 5L) == -5.0f);
    check("difftime across a 65536s boundary == 65536",
          difftime(65541L, 5L) == 65536.0f);
    check("difftime(t,t) == 0", difftime(12345L, 12345L) == 0.0f);

    if (fails) {
        printf("ttime failed: %d\n", fails);
        return 1;
    }
    printf("ttime passed\n");
    return 0;
}
