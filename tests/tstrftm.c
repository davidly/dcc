#include <stdio.h>
#include <string.h>
#include <time.h>

static int fails;

static void fail(const char *msg)
{
    printf("FAIL %s\n", msg);
    fails++;
}

int main(void)
{
    char buf[8];
    struct tm tmv;
    size_t n;

    memset(buf, 0x5a, sizeof(buf));
    memset(&tmv, 0, sizeof(tmv));

    tmv.tm_sec = 56;
    tmv.tm_min = 34;
    tmv.tm_hour = 12;
    tmv.tm_mday = 17;
    tmv.tm_mon = 7;
    tmv.tm_year = 126;
    tmv.tm_wday = 1;
    tmv.tm_yday = 228;
    tmv.tm_isdst = 0;

    n = strftime(buf, sizeof(buf), "", &tmv);
    if (n != 0)
        fail("strftime empty format should return current stub value 0");
    if (buf[0] != 0x5a)
        fail("strftime stub should leave buffer untouched");

    if (fails != 0) {
        printf("tstrftm FAILED: %d\n", fails);
        return 1;
    }

    printf("tstrftm: all tests passed\n");
    return 0;
}
