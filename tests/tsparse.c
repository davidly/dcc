/* tsparse.c - seeking far past current end-of-file and writing a single
 * byte extends the file's *tracked* length (dccrtl's __extln, driven off
 * the write offset) immediately, but only the one 128-byte record actually
 * touched by that write goes through a real BDOS random-write call.
 * Earlier records the seek jumped over are never written at all, so
 * whether reading them back afterward returns zero bytes, Ctrl-Z-filled
 * bytes (what dccrtl's own partial-record fill uses - see __zerdm), or a
 * short read depends on allocation details this test doesn't control. This
 * doesn't assert a specific "correct" fill value - it reports the reported
 * length, what actually comes back from the gap, and confirms the one byte
 * that was actually written survives at the right offset, so behavior is
 * at least visible and comparable across emulators.
 */
#include <stdio.h>

static int fails;

static void chki(const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s: got %d expected %d\n", name, got, expected);
        fails++;
    } else {
        printf("PASS %s: %d\n", name, got);
    }
}

int main(void)
{
    FILE *f;
    long len;
    int i, n;
    unsigned char buf[300];

    unlink("TSPRS.TMP");

    f = fopen("TSPRS.TMP", "w");
    if (!f) { printf("FAIL create\n"); return 1; }
    fseek(f, 5000, SEEK_SET);
    fputc('Z', f);
    fclose(f);

    f = fopen("TSPRS.TMP", "r");
    if (!f) { printf("FAIL reopen\n"); return 1; }

    /* Reported not asserted: CP/M's native unit is the 128-byte record, so
     * a length rounded up to record granularity (e.g. 5120, not the exact
     * 5001) is defensible rather than clearly wrong the way the append and
     * padding-beyond-length bugs are. Still worth comparing across
     * emulators - it's exactly the kind of thing that could differ. */
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    printf("REPORT reported length after gap write: %ld\n", len);
    printf("REPORT length is exact (5001): %d, rounded to 128 (5120): %d\n",
           len == 5001, len == 5120);

    /* Read the first 256 bytes of the gap and report their fill value(s). */
    fseek(f, 0, SEEK_SET);
    n = (int) fread(buf, 1, sizeof(buf), f);
    printf("REPORT bytes actually read from offset 0: %d\n", n);
    if (n > 0) {
        int allsame = 1;
        for (i = 1; i < n; i++)
            if (buf[i] != buf[0]) { allsame = 0; break; }
        if (allsame)
            printf("REPORT gap fill byte value: 0x%02x (uniform)\n", buf[0]);
        else
            printf("REPORT gap fill is not a uniform byte value (first byte 0x%02x)\n", buf[0]);
    }

    /* Whatever the gap contains, the one byte actually written must
     * survive intact at its exact offset. */
    fseek(f, 5000, SEEK_SET);
    n = fgetc(f);
    printf("REPORT byte at offset 5000: %d ('%c')\n", n, (n >= 32 && n < 127) ? n : '?');
    chki("written_byte_survives", n, 'Z');

    fclose(f);
    unlink("TSPRS.TMP");

    if (fails)
        printf("tsparse FAILED %d\n", fails);
    else
        printf("tsparse ok\n");
    return fails ? 1 : 0;
}
