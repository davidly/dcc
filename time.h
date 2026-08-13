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

/** Convert broken-down time *tp (year/mon/mday/hour/min/sec) to a time_t,
 *  and normalize *tp in place - out-of-range fields (e.g. tm_mday=32) carry
 *  into the next unit, and tm_wday/tm_yday are always recomputed. Supports
 *  years >= 1970; returns (time_t)-1 (and leaves *tp unmodified) for a NULL
 *  tp, an earlier year, or a result that doesn't fit in a 32-bit time_t. */
time_t mktime(struct tm *tp);

/** Convert *tp to a string of the form "Www Mmm dd hh:mm:ss yyyy\n" in an
 *  internal static buffer (not reentrant). Returns NULL if tp is NULL. */
char *asctime(const struct tm *tp);

/** Equivalent to asctime(localtime(tp)). Returns NULL if tp is NULL. */
char *ctime(const time_t *tp);

/** Convert *tp to broken-down time in an internal static buffer (not
 *  reentrant). Returns NULL if tp is NULL or *tp is negative. */
struct tm *gmtime(const time_t *tp);

/** Identical to gmtime(tp): this target has no timezone database, so the
 *  BDOS clock's raw reading already *is* "local" time, with no UTC offset
 *  to apply (see time()'s own comment above). */
struct tm *localtime(const time_t *tp);

/** Format broken-down time *tp into s according to fmt; returns 0 on CP/M 2.2. */
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tp);

#endif /* _TIME_H */
