/* tbufex.c - worked example: buffered console output with a user-declared
 * buffer.  Verifies the doc snippet compiles, runs, and batches output. */

#include <stdio.h>
#include <stdlib.h>

/* --8<-- [start:example] */
int main(void)
{
    static char obuf[1024];   /* user-declared console buffer */
    int i;
    long total;

    /* Adopt obuf and fully buffer: output accumulates instead of going to the
     * BDOS one character at a time. */
    if (setvbuf(stdout, obuf, _IOFBF, sizeof obuf) != 0) {
        puts("setvbuf failed");
        return 1;
    }

    total = 0;
    for (i = 1; i <= 20; i = i + 1) {
        printf("row %2d: %ld\n", i, total);
        total = total + (long) i * i;
    }

    /* Nothing has reached the console yet (fully buffered, under 1 KB).
     * Drain it explicitly. */
    fflush(stdout);

    printf("sum of squares 1..20 = %ld\n", total);

    /* Flush before detaching: switching buffers while output is still
     * pending in the old one is unspecified behavior (some libc's drop it
     * instead of auto-flushing). */
    fflush(stdout);

    /* Detach the buffer before it goes out of scope / is reused, so the
     * automatic flush at exit uses the internal buffer. */
    setvbuf(stdout, (char *) 0, _IOLBF, 0);

    return 0;
}
/* --8<-- [end:example] */
