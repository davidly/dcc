#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>

/** Processor time type; same width as long on this target. */
typedef long clock_t;
/** Calendar time type; same width as long on this target. */
typedef long time_t;

/** clock() ticks per second.  CP/M 2.2 has no clock; clock() returns -1. */
#define CLOCKS_PER_SEC 1

/** Broken-down calendar time. */
struct tm {
    int tm_sec;   /** Seconds [0,60]. */
    int tm_min;   /** Minutes [0,59]. */
    int tm_hour;  /** Hours [0,23]. */
    int tm_mday;  /** Day of month [1,31]. */
    int tm_mon;   /** Months since January [0,11]. */
    int tm_year;  /** Years since 1900. */
    int tm_wday;  /** Days since Sunday [0,6]. */
    int tm_yday;  /** Days since January 1 [0,365]. */
    int tm_isdst; /** Daylight Saving Time flag. */
};

/** Processor time used since program start; returns (clock_t)-1 (unavailable on CP/M 2.2). */
clock_t clock(void);

/** Current calendar time; stores through tp if non-null.
 *  Reads a real clock via BDOS function 105 (CP/M 3+ "Get Date and Time")
 *  when the underlying system/emulator implements it - many do even while
 *  still reporting CP/M 2.2 via BDOS 12. The result tracks the BDOS clock's
 *  raw wall-clock reading with no timezone adjustment (CP/M has no timezone
 *  concept), encoded as a Unix time_t. Returns (time_t)-1, and stores -1
 *  through tp, when the underlying BDOS doesn't actually implement the
 *  call (e.g. real CP/M 2.2 hardware). */
time_t time(time_t *tp);

/** Difference t1-t0 as a floating-point count of seconds.
 *  Note: C89 returns double; dcc returns float (no double type). */
float difftime(time_t t1, time_t t0);

/** Convert broken-down time *tp to a time_t; returns (time_t)-1 (unavailable). */
time_t mktime(struct tm *tp);

/** Convert *tp to a string of the form "Www Mmm dd hh:mm:ss yyyy\n".
 *  Returns NULL on CP/M 2.2. */
char *asctime(const struct tm *tp);

/** Convert the calendar time *tp to a local-time string; returns NULL on CP/M 2.2. */
char *ctime(const time_t *tp);

/** Convert *tp to UTC broken-down time; returns NULL on CP/M 2.2. */
struct tm *gmtime(const time_t *tp);

/** Convert *tp to local broken-down time; returns NULL on CP/M 2.2. */
struct tm *localtime(const time_t *tp);

/** Format broken-down time *tp into s according to fmt; returns 0 on CP/M 2.2. */
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tp);

#endif /* _TIME_H */
