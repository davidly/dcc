/* tsnprtf.c - snprintf/vsnprintf (C99) truncation and return-value coverage.
 * Plain build (no -ffloatio/-flongio) - tpfio.c/tplng.c/tpflio.c cover the
 * flag combinations, since -ffloatio/-flongio must remap snprintf/vsnprintf
 * the same way they remap the rest of the printf family. */
#include <stdio.h>
#include <stdarg.h>

static void call_vsnprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, n, fmt, ap);
    va_end(ap);
}

int main(void)
{
    char buf[32];
    int r;

    /* Plenty of room: behaves like sprintf, same return value. */
    r = snprintf(buf, sizeof(buf), "hi %d", 42);
    printf("[%s] r=%d\n", buf, r);

    /* Exact fit: n-1 chars + NUL. */
    r = snprintf(buf, 6, "abcde");
    printf("[%s] r=%d\n", buf, r);

    /* Truncation: logical output longer than n; buf holds only n-1 chars +
     * NUL, but the return value is the length as if unbounded (C99). */
    r = snprintf(buf, 5, "abcdefgh");
    printf("[%s] r=%d\n", buf, r);

    /* n=1: only room for the NUL, no content bytes at all. */
    r = snprintf(buf, 1, "xyz");
    printf("[%s] r=%d\n", buf, r);

    /* n=0: buf must be left completely untouched, not even a NUL. */
    buf[0] = 'Q';
    r = snprintf(buf, 0, "xyz");
    printf("[%c] r=%d\n", buf[0], r);

    /* Multiple truncated calls in a row must not leak state between calls
     * (each call resets its own cap fresh from n). */
    r = snprintf(buf, 3, "aa");
    printf("[%s] r=%d\n", buf, r);
    r = snprintf(buf, 3, "bbbb");
    printf("[%s] r=%d\n", buf, r);

    /* vsnprintf: truncation via varargs, multiple conversions. */
    call_vsnprintf(buf, 4, "%d-%d-%d", 11, 22, 33);
    printf("[%s]\n", buf);

    /* vsnprintf: plenty of room. */
    call_vsnprintf(buf, sizeof(buf), "%s=%d", "ans", 42);
    printf("[%s]\n", buf);

    printf("tsnprtf ok\n");
    return 0;
}
