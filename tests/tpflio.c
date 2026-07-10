/* tpflio.c - combined float+long printf runtime entry coverage (_pflio and
 * friends: -ffloatio and -flongio together). Confirms %f and %ld/%lu/%lx
 * both work, and both are shared identically across the whole printf family
 * (sprintf/fprintf/vprintf/vsprintf/vfprintf), not just printf(). */
#include <stdio.h>
#include <stdarg.h>

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

static void call_vsnprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, n, fmt, ap);
    va_end(ap);
}

int main(void)
{
    char buf[48];
    int r;
    float f;
    long sl;
    unsigned long ul;

    f = 3.75f;
    sl = -123456L;
    ul = 0x89ABCDEFUL;

    printf("tpflio start\n");
    printf("f=%.2f sl=%ld ul=%lu\n", f, sl, ul);

    sprintf(buf, "sp=%.2f/%ld", f, sl);
    printf("%s\n", buf);

    fprintf(stdout, "fp=%.2f/%lu\n", f, ul);

    call_vprintf("vp=%.2f/%lx\n", f, ul);

    call_vsprintf(buf, "vs=%.2f/%ld", f, sl);
    printf("%s\n", buf);

    call_vfprintf(stdout, "vf=%.2f/%lu\n", f, ul);

    r = snprintf(buf, sizeof(buf), "sn=%.2f/%ld", f, sl);
    printf("%s r=%d\n", buf, r);

    r = snprintf(buf, 6, "sn=%.2f/%ld", f, sl);
    printf("%s r=%d\n", buf, r);

    call_vsnprintf(buf, sizeof(buf), "vn=%.2f/%lu", f, ul);
    printf("%s\n", buf);

    printf("tpflio ok\n");
    return 0;
}
