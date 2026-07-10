/* tvapinit.c - pointer-typed va_arg used directly as a declaration initializer.
 *
 * `const char *s = va_arg(ap, const char *);` combines a pointer result type
 * with declaration-initializer context, the case that previously had to be
 * split into a separate declaration and assignment. The C99 mid-block
 * declaration inside the loop is deliberate - it is the shape under test.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

/* Join `count` C strings into dst with a separator; returns total length. */
static int join(char *dst, const char *sep, int count, ...)
{
    va_list ap;
    int32_t pos = 0;
    int i;

    va_start(ap, count);
    for (i = 0; i < count; i++) {
        const char *s = va_arg(ap, const char *);  /* pointer va_arg initializer */

        if (i > 0) {
            strcpy(dst + pos, sep);
            pos += (int32_t)strlen(sep);
        }
        strcpy(dst + pos, s);
        pos += (int32_t)strlen(s);
    }
    va_end(ap);
    return (int)pos;
}

int main(void)
{
    char buf[64];
    int len = join(buf, ", ", 4, "alpha", "beta", "gamma", "delta");
    int commas = 0;
    int i;

    for (i = 0; buf[i]; i++) {
        if (buf[i] == ',')
            commas++;
    }

    printf("tvapinit len=%d commas=%d str=%s\n", len, commas, buf);
    return 0;
}
