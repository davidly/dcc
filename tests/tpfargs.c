/* tpfargs.c - printf argument parsing, bounds, and write-error coverage. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static int failures;

static void check_int(const char *name, int got, int want)
{
    if (got != want) {
        printf("%s: got %d want %d\n", name, got, want);
        failures++;
    }
}

static void check_str(const char *name, const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("%s: got [%s] want [%s]\n", name, got, want);
        failures++;
    }
}

static int call_vfprintf(FILE *fp, const char *fmt, ...)
{
    int r;
    va_list ap;

    va_start(ap, fmt);
    r = vfprintf(fp, fmt, ap);
    va_end(ap);
    return r;
}

int main(void)
{
    static char text[260];
    char buf[48];
    int i;
    int r;

    r = snprintf(buf, sizeof(buf), "%li|%d", -123456L, 17);
    check_int("long count", r, 10);
    check_str("long cursor", buf, "-123456|17");

    r = snprintf(buf, sizeof(buf), "%lf|%d", 1.5f, 23);
    check_int("float count", r, 11);
    check_str("float cursor", buf, "1.500000|23");

    for (i = 0; i < 259; i++)
        text[i] = 'x';
    text[259] = 0;

    r = snprintf(buf, sizeof(buf), "%255dZ", 7);
    check_int("width 255", r, 256);
    r = snprintf(buf, sizeof(buf), "%256dZ", 7);
    check_int("width 256 saturates", r, 256);
    r = snprintf(buf, sizeof(buf), "%9999dZ", 7);
    check_int("width overflow sticks", r, 256);

    r = snprintf(buf, sizeof(buf), "%.255sZ", text);
    check_int("precision 255", r, 256);
    r = snprintf(buf, sizeof(buf), "%.256sZ", text);
    check_int("precision 256 saturates", r, 256);

    r = snprintf(buf, sizeof(buf), "%255.255dZ", -1);
    check_int("signed field 255", r, 257);
    r = snprintf(buf, sizeof(buf), "%256.256dZ", -1);
    check_int("signed field overflow", r, 257);

    r = fprintf((FILE *)99, "bad:%d", 7);
    check_int("fprintf bad fd", r, EOF);
    r = call_vfprintf((FILE *)99, "vbad:%d", 9);
    check_int("vfprintf bad fd", r, EOF);

    if (failures != 0) {
        printf("tpfargs failed: %d\n", failures);
        return 1;
    }
    puts("tpfargs ok");
    return 0;
}
