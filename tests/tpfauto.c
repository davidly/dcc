/* tpfauto.c - printf-family per-call-site auto-detection (no -ffloatio/
 * -flongio passed at all). Each call's own literal format string determines
 * which runtime entry point it needs; mixing plain/float/long calls to the
 * same underlying C function in one file must resolve each independently.
 * A non-literal format string (passed through a helper) must conservatively
 * get both float and long support. */
#include <stdio.h>
#include <stdarg.h>

static void log_float(const char *fmt, float x)
{
    /* fmt is a parameter here, not a literal - dcc can't see into it at
     * this call site, so this call must conservatively support both %f
     * and %ld even though this particular fmt only uses %f. */
    printf(fmt, x);
}

int main(void)
{
    char buf[40];
    long sl;

    sl = -123456L;

    /* plain: no %f, no %l - smallest entry point for each. */
    printf("plain %d %s\n", 42, "ok");
    sprintf(buf, "plainsp %d", 7);
    printf("%s\n", buf);

    /* float-only: same underlying functions, different call sites. */
    printf("float %.2f\n", 1.5f);
    sprintf(buf, "floatsp %.2f", 2.5f);
    printf("%s\n", buf);

    /* long-only. */
    printf("long %ld\n", sl);
    sprintf(buf, "longsp %ld", sl);
    printf("%s\n", buf);

    /* both float and long in the very same call. */
    printf("both %.1f %ld\n", 3.5f, sl);

    /* non-literal format string: conservative fallback. */
    log_float("dyn %.1f\n", 4.5f);

    /* hex/octal-only: neither needs float/long support. */
    printf("hex %x %X\n", 255, 255);
    sprintf(buf, "hexsp %x", 4096);
    printf("%s\n", buf);
    printf("oct %o\n", 8);
    sprintf(buf, "octsp %o", 511);
    printf("%s\n", buf);

    /* %lx: long hex uses the already-gated long path, not the hex hooks. */
    printf("lx %lx\n", 0x89ABCDEFUL);

    /* hex/octal mixed with float/long in the same call. */
    printf("mix %x %.1f %o %ld\n", 255, 1.5f, 8, sl);

    printf("tpfauto ok\n");
    return 0;
}
