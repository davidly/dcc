/* Exercise every representable calendar day and the /86400 split boundaries. */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const int month_days[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static int check_time(time_t stamp, int year, int month, int mday,
                      int wday, int yday, int hour, int minute, int second)
{
    struct tm *p;

    p = gmtime(&stamp);
    if (p != NULL && p->tm_year == year - 1900 && p->tm_mon == month &&
        p->tm_mday == mday && p->tm_wday == wday && p->tm_yday == yday &&
        p->tm_hour == hour && p->tm_min == minute && p->tm_sec == second &&
        p->tm_isdst == 0)
        return 1;
    printf("FAIL gmtime at %ld\n", stamp);
    return 0;
}

int main(void)
{
    time_t start, stamp;
    struct tm saved, normalized;
    struct tm *shared;
    int day, year, month, mday, wday, yday, length, leap;
    int hour, minute, second, i;
    static const long edges[] = {
        0L, 1L, 59L, 60L, 127L, 128L, 255L, 256L,
        3599L, 3600L, 65535L, 65536L, 86399L
    };

    /* Explicit clock edges cover the seven discarded bits and the carry
       into the seventeenth bit of the seconds-within-day remainder. */
    for (i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i) {
        stamp = edges[i];
        if (!check_time(stamp, 1970, 0, 1, 4, 0,
                        (int)(stamp / 3600L),
                        (int)((stamp / 60L) % 60L), (int)(stamp % 60L)))
            return 1;
    }

    /* Advance the reference date by month lengths rather than converting
       seconds back into dates using the runtime's calendar arithmetic. */
    start = 0L;
    year = 1970;
    month = 0;
    mday = 1;
    wday = 4;
    yday = 0;
    for (day = 0; day <= 24855; ++day) {
        if (!check_time(start, year, month, mday, wday, yday, 0, 0, 0))
            return 1;
        if (day == 24855) {
            if (!check_time(2147483647L, year, month, mday,
                            wday, yday, 3, 14, 7))
                return 1;
            break;
        }
        if (!check_time(start + 86399L, year, month, mday,
                        wday, yday, 23, 59, 59))
            return 1;
        hour = day % 24;
        minute = day % 60;
        second = (day / 60) % 60;
        stamp = start + (long)hour * 3600L + (long)minute * 60L + second;
        if (!check_time(stamp, year, month, mday,
                        wday, yday, hour, minute, second))
            return 1;

        start += 86400L;
        wday = (wday + 1) % 7;
        ++yday;
        leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        length = month_days[month];
        if (month == 1 && leap)
            ++length;
        if (++mday > length) {
            mday = 1;
            if (++month == 12) {
                month = 0;
                ++year;
                yday = 0;
            }
        }
    }

    stamp = 2147483647L;
    shared = gmtime(&stamp);
    saved = *shared;
    stamp = -1L;
    if (gmtime(NULL) != NULL || gmtime(&stamp) != NULL ||
        memcmp(shared, &saved, sizeof(saved)) != 0) {
        printf("FAIL invalid input preserves shared object\n");
        return 1;
    }
    stamp = LONG_MIN;
    if (gmtime(&stamp) != NULL || memcmp(shared, &saved, sizeof(saved)) != 0) {
        printf("FAIL minimum signed time\n");
        return 1;
    }
    stamp = 2147483647L;
    if (localtime(&stamp) != shared || memcmp(shared, &saved, sizeof(saved)) != 0) {
        printf("FAIL localtime shares gmtime result\n");
        return 1;
    }
    memset(&normalized, 0, sizeof(normalized));
    normalized.tm_year = 100;
    normalized.tm_mon = 1;
    normalized.tm_mday = 29;
    if (mktime(&normalized) != 951782400L || normalized.tm_yday != 59 ||
        memcmp(shared, &saved, sizeof(saved)) != 0 ||
        mktime(shared) != 2147483647L || memcmp(shared, &saved, sizeof(saved)) != 0) {
        printf("FAIL mktime breakdown and shared result\n");
        return 1;
    }
    printf("tgmtime passed\n");
    return 0;
}
