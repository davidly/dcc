#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>

/** Processor time type; same width as long on this target. */
typedef long clock_t;
/** Signed 32-bit calendar time type; same representation as long. */
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
 *  call (e.g. real CP/M 2.2 hardware), supplies invalid packed BCD, or reports
 *  a value after 2038-01-19 03:14:07. CP/M day 1 is 1978-01-01; the runtime's
 *  conversion therefore maps CP/M day 0 to Unix day 2921 (1977-12-31). */
time_t time(time_t *tp);

/** Difference t1-t0 as a floating-point count of seconds.
 *  The subtraction is evaluated without signed-long overflow before rounding
 *  the mathematical result to the target's single-precision float.
 *  Note: C89 returns double; dcc returns float (no double type). */
float difftime(time_t t1, time_t t0);

/** Convert broken-down time *tp (year/mon/mday/hour/min/sec) to a time_t,
 *  and normalize *tp in place. All six input fields may be out of range and
 *  are combined before the representability check, so e.g. 1969-12-32 becomes
 *  1970-01-01. tm_wday/tm_yday are always recomputed. Returns (time_t)-1 and
 *  leaves *tp byte-for-byte unchanged for NULL, a normalized pre-1970 value,
 *  or any value after the signed 32-bit maximum (including 2106-era unsigned
 *  bit patterns and values that would wrap modulo 2^32). It does not modify
 *  the shared object previously returned by gmtime/localtime when tp points
 *  to an unrelated object. */
time_t mktime(struct tm *tp);

/** Convert *tp to a string of the form "Www Mmm dd hh:mm:ss yyyy\n" in an
 *  internal 26-byte static buffer (not reentrant). Returns NULL without
 *  modifying that buffer if tp is NULL, weekday/month is out of range, any
 *  numeric clock/date field is outside its struct tm range, or the actual
 *  year is outside 0000..9999. */
char *asctime(const struct tm *tp);

/** Equivalent to asctime(localtime(tp)). Returns NULL if tp is NULL. */
char *ctime(const time_t *tp);

/** Convert *tp to broken-down time in an internal static buffer (not
 *  reentrant). Returns NULL if tp is NULL or *tp is negative; time_t bit
 *  patterns 0x80000000 through 0xffffffff are therefore rejected. */
struct tm *gmtime(const time_t *tp);

/** Identical to gmtime(tp): this target has no timezone database, so the
 *  BDOS clock's raw reading already *is* "local" time, with no UTC offset
 *  to apply (see time()'s own comment above). */
struct tm *localtime(const time_t *tp);

/* Format broken-down time *tp into s in the fixed C locale.
 *  Supports the C89 conversions %a, %A, %b, %B, %c, %d, %H, %I,
 *  %j, %m, %M, %p, %S, %U, %w, %W, %x, %X, %y, %Y, %Z,
 *  and %%, plus %C for the calendar century (year/100, at least two
 *  digits). %Z is empty because this target has no timezone database.
 *  The composite C-locale forms are "Www Mmm dd hh:mm:ss yyyy",
 *  "mm/dd/yy", and "hh:mm:ss"; %c space-pads a one-digit day.
 *
 *  max==0 returns 0 without reading or writing through any pointer. With
 *  max>0, s, fmt, and tp must be non-NULL. The range-bearing struct tm
 *  fields must be within their documented ranges, except tm_year may range
 *  from -1900 through INT_MAX (calendar years 0 through 34667); tm_isdst is
 *  ignored and cross-field calendar consistency is not checked.
 *
 *  Unknown conversions and a trailing '%' are rejected deterministically.
 *  On invalid input or insufficient space, returns 0 and leaves a
 *  NUL-terminated prefix in a non-NULL destination. On success, returns the
 *  number of bytes written before the NUL; an empty result also returns 0. */
/** Format broken-down time in the fixed C locale. */
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tp);

#endif /* _TIME_H */
