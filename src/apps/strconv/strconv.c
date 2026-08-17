/**
 * @file strconv.c
 * @brief Defines C89 sources for dcc runtime string conversion and tokenising.
 *
 * @par Role
 * Provides the source-of-truth implementations of strtol(), strtoul(), and
 * strtok() that are compiled with dcc, optimized, label-renamed, and merged
 * into DCCRTL.MAC. The conversion routines report endpoints and range errors;
 * strtok() keeps its required scan state. There is no main() or console I/O.
 *
 * @par Key entry points
 * strtol(), strtoul(), and strtok().
 *
 * @par Boundary
 * Existing DCCRTL helpers provide long arithmetic, errno, strspn(), and
 * strpbrk(); dcc_asmname.c maps the public names to __stol, __stou, and
 * __stok for LINK-80. tests/tstrconv.c validates the merged runtime code.
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

/* True for the C locale white-space characters (space, \t \n \v \f \r). */
static int sc_isspace(int c)
{
    return c == ' ' || (c >= 9 && c <= 13);
}

/* Map a digit/letter to its value 0..35, or 99 if not alphanumeric. */
static int sc_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

long strtol(const char *nptr, char **endptr, int base)
{
    const char *s;
    int neg;
    int any;
    int d;
    unsigned long acc;
    unsigned long cutoff;
    int cutlim;

    s = nptr;
    while (sc_isspace((unsigned char)*s)) s++;

    neg = 0;
    if (*s == '+') {
        s++;
    } else if (*s == '-') {
        neg = 1;
        s++;
    }

    if ((base == 0 || base == 16) &&
        s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
        sc_digit((unsigned char)s[2]) < 16) {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = (s[0] == '0') ? 8 : 10;
    }

    /* Largest magnitude representable, split for overflow detection. */
    cutoff = neg ? (unsigned long)LONG_MAX + 1UL : (unsigned long)LONG_MAX;
    cutlim = (int)(cutoff % (unsigned long)base);
    cutoff = cutoff / (unsigned long)base;

    acc = 0;
    any = 0;
    for (;; s++) {
        d = sc_digit((unsigned char)*s);
        if (d >= base) break;
        if (any < 0 || acc > cutoff || (acc == cutoff && d > cutlim)) {
            any = -1;
        } else {
            any = 1;
            acc = acc * (unsigned long)base + (unsigned long)d;
        }
    }

    if (any < 0) {
        acc = neg ? (unsigned long)LONG_MIN : (unsigned long)LONG_MAX;
        errno = ERANGE;
    } else if (neg) {
        acc = (unsigned long)(0UL - acc);
    }

    if (endptr != NULL)
        *endptr = (char *)(any ? s : nptr);

    return (long)acc;
}

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    const char *s;
    int neg;
    int any;
    int d;
    unsigned long acc;
    unsigned long cutoff;
    int cutlim;

    s = nptr;
    while (sc_isspace((unsigned char)*s)) s++;

    neg = 0;
    if (*s == '+') {
        s++;
    } else if (*s == '-') {
        neg = 1;
        s++;
    }

    if ((base == 0 || base == 16) &&
        s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
        sc_digit((unsigned char)s[2]) < 16) {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = (s[0] == '0') ? 8 : 10;
    }

    cutoff = ULONG_MAX / (unsigned long)base;
    cutlim = (int)(ULONG_MAX % (unsigned long)base);

    acc = 0;
    any = 0;
    for (;; s++) {
        d = sc_digit((unsigned char)*s);
        if (d >= base) break;
        if (any < 0 || acc > cutoff || (acc == cutoff && d > cutlim)) {
            any = -1;
        } else {
            any = 1;
            acc = acc * (unsigned long)base + (unsigned long)d;
        }
    }

    if (any < 0) {
        acc = ULONG_MAX;
        errno = ERANGE;
    } else if (neg) {
        acc = (unsigned long)(0UL - acc);
    }

    if (endptr != NULL)
        *endptr = (char *)(any ? s : nptr);

    return acc;
}

/* Saved scan position between strtok calls (C89 7.11.5.8). */
static char *sc_strtok_last;

char *strtok(char *s, const char *delim)
{
    char *tok;

    if (s == NULL)
        s = sc_strtok_last;
    if (s == NULL)
        return NULL;

    /* Skip leading delimiters. */
    s = s + strspn(s, delim);
    if (*s == '\0') {
        sc_strtok_last = NULL;
        return NULL;
    }

    tok = s;
    s = strpbrk(tok, delim);
    if (s == NULL) {
        sc_strtok_last = NULL;
    } else {
        *s = '\0';
        sc_strtok_last = s + 1;
    }
    return tok;
}
