/* tvplain.c - no-long va_list printf-family runtime entry coverage. */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static int failures;

static void check_str(const char *name, const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("FAIL %s got '%s' want '%s'\n", name, got, want);
        failures++;
    }
}

static void call_vprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static void call_vfprintf(FILE *fp, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
}

static void call_vsprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
}

int main(void)
{
    char buf[40];

    printf("tvplain start\n");
    call_vprintf("vp:%d:%s:%x\n", -12, "ok", 0x2a);
    call_vfprintf(stdout, "vf:%u:%c:%s\n", 65535U, 'Z', "done");
    call_vsprintf(buf, "vs:%d:%s:%04x", 17, "buf", 31);
    check_str("vsprintf", buf, "vs:17:buf:001f");

    if (failures != 0)
        return 1;

    printf("vs:%s\n", buf);
    printf("tvplain ok\n");
    return 0;
}