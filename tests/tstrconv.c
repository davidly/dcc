/*
 * tstrconv.c - verification of the C89 string-conversion/tokenising additions
 * to the dcc runtime: strtol, strtoul, strtok, and the vprintf/vfprintf/
 * vsprintf family.
 *
 * Output is deterministic so it can be diffed against baseline_test_dcc.txt.
 * The vprintf/vfprintf console lines are themselves the test for those two
 * functions (their exact text is checked by the baseline diff); vsprintf is
 * checked in-program via strcmp.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>

static int checks = 0;
static int failures = 0;

static void oki(const char *name, int got, int want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s: got %d want %d\n", name, got, want);
    }
}

static void okl(const char *name, long got, long want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s: got %ld want %ld\n", name, got, want);
    }
}

static void okul(const char *name, unsigned long got, unsigned long want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s: got %lu want %lu\n", name, got, want);
    }
}

static void oks(const char *name, const char *got, const char *want)
{
    checks++;
    if (got == NULL || strcmp(got, want) != 0) {
        failures++;
        printf("FAIL %s\n", name);
    }
}

/* va_list relays for the v* family. */
static int vs(char *buf, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

static void vp(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static void vfp(FILE *fp, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
}

int main(void)
{
    char *end;
    char tbuf[40];
    char vb[64];
    char bmax[40], bmin[40], bumax[40], bov[44];
    long lv;
    unsigned long uv;
    char *t;
    int n;

    printf("=== dcc strconv verification ===\n");

    /* ---- strtol ---- */
    okl("strtol dec", strtol("123", NULL, 10), 123L);
    okl("strtol neg", strtol("-456", NULL, 10), -456L);
    okl("strtol +ws", strtol("   +789", NULL, 10), 789L);
    okl("strtol hex", strtol("0x1A", NULL, 16), 26L);
    okl("strtol auto hex", strtol("0x1f", NULL, 0), 31L);
    okl("strtol auto oct", strtol("010", NULL, 0), 8L);
    okl("strtol base16 noprefix", strtol("FF", NULL, 16), 255L);
    okl("strtol base36", strtol("z", NULL, 36), 35L);
    /* Boundary inputs are built from the platform's own limit macros so the
     * test adapts to dcc's 32-bit long and a host's wider long alike. */
    sprintf(bmax, "%ld", LONG_MAX);
    okl("strtol LONG_MAX", strtol(bmax, NULL, 10), LONG_MAX);
    sprintf(bmin, "%ld", LONG_MIN);
    okl("strtol LONG_MIN", strtol(bmin, NULL, 10), LONG_MIN);

    lv = strtol("12ab", &end, 10);
    okl("strtol endptr val", lv, 12L);
    oki("strtol endptr pos", (int)*end, 'a');

    /* one decimal digit more than LONG_MAX -> guaranteed overflow at any width */
    sprintf(bov, "%ld0", LONG_MAX);
    errno = 0;
    lv = strtol(bov, NULL, 10);
    okl("strtol ovf val", lv, LONG_MAX);
    oki("strtol ovf errno", errno, ERANGE);

    /* one decimal digit more negative than LONG_MIN -> guaranteed negative
     * overflow at any width; the sign-and-overflow clamp path (distinct
     * from the positive-overflow path above) is only reached this way. */
    sprintf(bov, "%ld0", LONG_MIN);
    errno = 0;
    lv = strtol(bov, NULL, 10);
    okl("strtol neg ovf val", lv, LONG_MIN);
    oki("strtol neg ovf errno", errno, ERANGE);

    /* ---- strtoul ---- */
    sprintf(bumax, "%lu", ULONG_MAX);
    okul("strtoul max", strtoul(bumax, NULL, 10), ULONG_MAX);
    okul("strtoul hex", strtoul("0xFFFFFFFF", NULL, 16), 0xFFFFFFFFUL);
    okul("strtoul neg wraps", strtoul("-1", NULL, 10), ULONG_MAX);

    sprintf(bov, "%lu0", ULONG_MAX);
    errno = 0;
    uv = strtoul(bov, NULL, 10);
    okul("strtoul ovf val", uv, ULONG_MAX);
    oki("strtoul ovf errno", errno, ERANGE);

    /* ---- strtok ---- */
    strcpy(tbuf, "a,b,,c");
    t = strtok(tbuf, ",");
    oks("tok a", t, "a");
    t = strtok(NULL, ",");
    oks("tok b", t, "b");
    t = strtok(NULL, ",");
    oks("tok c (skips empty)", t, "c");
    t = strtok(NULL, ",");
    oki("tok end null", (int)(t == NULL), 1);

    strcpy(tbuf, "  hello   world  ");
    t = strtok(tbuf, " ");
    oks("tok hello", t, "hello");
    t = strtok(NULL, " ");
    oks("tok world", t, "world");
    t = strtok(NULL, " ");
    oki("tok trail null", (int)(t == NULL), 1);

    /* ---- vsprintf (checked in-program) ---- */
    n = vs(vb, "%d/%s/%ld", 42, "hi", 123L);
    oks("vsprintf out", vb, "42/hi/123");
    oki("vsprintf ret", n, 9);

    n = vs(vb, "%d", 12345);
    oks("vsprintf int", vb, "12345");
    oki("vsprintf int ret", n, 5);

    /* Regression guard for the pf_arg_dehl BC-clobber bug: a %ld conversion
     * must not corrupt the running character count that printf/sprintf/
     * vsprintf return.  The strings were always correct, so only the count
     * catches it. */
    oki("sprintf %ld count", sprintf(vb, "n=%ld!", 1000000L), 10);
    oki("printf  %ld count", printf("longcount:%ld\n", 123456L), 17);

    /* ---- vprintf / vfprintf (checked by baseline diff) ---- */
    vp("vprintf:%s=%d\n", "x", 7);
    vfp(stdout, "vfprintf:%u\n", 99);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
