/* tpfio.c - printf float-only runtime entry coverage (_pffio and friends).
 * Confirms %f support is shared identically across the whole printf family
 * (sprintf/fprintf/vprintf/vsprintf/vfprintf), not just plain printf(). */
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
    char buf[40];
    int r;
    float a;
    float b;

    a = 1.25f;
    b = 2.5f;

    printf("tpfio start\n");
    printf("a=%f\n", a);
    printf("b=%.2f\n", b);
    printf("sum=%f\n", a + b);
    printf("mix=%d %.1f %s\n", 7, b, "ok");

    sprintf(buf, "sp=%.2f", a + b);
    printf("%s\n", buf);

    fprintf(stdout, "fp=%.2f\n", a + b);

    call_vprintf("vp=%.2f\n", a + b);

    call_vsprintf(buf, "vs=%.2f", a + b);
    printf("%s\n", buf);

    call_vfprintf(stdout, "vf=%.2f\n", a + b);

    r = snprintf(buf, sizeof(buf), "sn=%.2f", a + b);
    printf("%s r=%d\n", buf, r);

    r = snprintf(buf, 4, "sn=%.2f", a + b);
    printf("%s r=%d\n", buf, r);

    call_vsnprintf(buf, sizeof(buf), "vn=%.2f", a + b);
    printf("%s\n", buf);

    printf("tpfio ok\n");
    return 0;
}
