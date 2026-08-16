/* tfeof.c - dedicated feof() regression coverage. Added alongside two
 * DCCRTL.MAC changes:
 *   - a simplification of _feof's fd-range check (merged into a single
 *     "sub FIRSTFD / cp NFILES", dropping the separate carry check)
 *   - a real fix to __fgetc/__getc: they detected EOF via _read()'s
 *     short-read return value and returned EOF correctly, but never set
 *     __fdeof, so feof() incorrectly stayed false after a fgetc()/getc()
 *     call that itself returned EOF (only fread()/fgets() set the flag) -
 *     a C89 7.9.7.5 violation. See "empty_file" below for the cleanest
 *     repro (no CP/M record-padding ambiguity to confuse it).
 * Existing feof() usage elsewhere (fileops.c/tctrlz.c/tioerr.c/a1.c) is
 * incidental (loop control or a single assertion), not a targeted check
 * of feof()'s state-transition semantics, so it wouldn't reliably catch
 * a regression in either of the above.
 *
 * Fixtures are exactly 128 bytes (one full CP/M record) so a full read
 * doesn't run into tpadread.c's separate, already-documented finding:
 * a *partial* final record is padded with 0x1A filler that binary reads
 * see as real data, which would make a short file's true EOF land later
 * than its written content - not what this file is testing.
 */
#include <stdio.h>
#include <unistd.h>

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

static void make_fixture(const char *path, int fillchar, int n)
{
    FILE *f = fopen(path, "wb");
    int i;
    for (i = 0; i < n; i++) fputc(fillchar, f);
    fclose(f);
}

int main(void)
{
    FILE *f, *g;
    char buf[256];
    int c, n;

    make_fixture("TFEA.TMP", 'A', 128);
    make_fixture("TFEB.TMP", 'B', 128);
    make_fixture("TFEC.TMP", 'x', 0);   /* truly empty */

    /* --- feof() is false right after open, before any read --- */
    f = fopen("TFEA.TMP", "rb");
    if (!f) { printf("FAIL open TFEA\n"); return 1; }
    chki("feof_false_after_open", feof(f) != 0, 0);

    /* --- stays false while consuming all 128 real bytes --- */
    n = 0;
    while (n < 128) {
        c = fgetc(f);
        if (c != 'A') { printf("FAIL byte %d got %d\n", n, c); fails++; }
        n++;
    }
    chki("feof_false_at_exact_end", feof(f) != 0, 0);

    /* --- becomes true only once a read is attempted *past* the end --- */
    c = fgetc(f);
    chki("read_past_end_is_EOF", c, EOF);
    chki("feof_true_after_crossing_end", feof(f) != 0, 1);

    /* --- stays true on repeated calls with no intervening read --- */
    chki("feof_still_true_on_recheck", feof(f) != 0, 1);
    chki("feof_still_true_on_recheck2", feof(f) != 0, 1);

    /* --- clearerr() resets it --- */
    clearerr(f);
    chki("feof_false_after_clearerr", feof(f) != 0, 0);

    /* Position is still at end-of-file, so the very next read sets EOF
     * again - clearerr() resets the flag, not the file position. */
    c = fgetc(f);
    chki("still_at_end_after_clearerr", c, EOF);
    chki("feof_true_again", feof(f) != 0, 1);

    fclose(f);

    /* --- per-stream, not global: exhausting one fd's EOF flag must not
     * affect a second, independently-open fd --- */
    f = fopen("TFEA.TMP", "rb");
    g = fopen("TFEB.TMP", "rb");
    if (!f || !g) { printf("FAIL open pair\n"); return 1; }

    n = 0;
    while (fgetc(f) != EOF) {
        n++;
        if (n > 400) break; /* safety: never spin forever on a runaway read */
    }
    chki("first_stream_at_eof", feof(f) != 0, 1);
    chki("second_stream_unaffected", feof(g) != 0, 0);

    c = fgetc(g);
    chki("second_stream_first_byte", c, 'B');
    chki("second_stream_still_not_eof", feof(g) != 0, 0);

    fclose(f);
    fclose(g);

    /* --- fread() hitting a short read at EOF also sets the flag --- */
    f = fopen("TFEB.TMP", "rb");
    if (!f) { printf("FAIL open TFEB for fread\n"); return 1; }
    n = (int) fread(buf, 1, sizeof buf, f);
    chki("fread_reads_exact_128", n, 128);
    chki("feof_true_after_short_fread", feof(f) != 0, 1);
    fclose(f);

    /* --- a truly empty file hits EOF on the very first read; feof()
     * must be set too - this is the exact scenario the __fgetc fix
     * targets (no CP/M record-padding involved, since nothing was ever
     * written: _read() returns 0 immediately). --- */
    f = fopen("TFEC.TMP", "rb");
    if (!f) { printf("FAIL open TFEC\n"); return 1; }
    chki("feof_false_on_fresh_empty_file", feof(f) != 0, 0);
    c = fgetc(f);
    chki("empty_file_first_read_is_EOF", c, EOF);
    chki("feof_true_on_empty_file", feof(f) != 0, 1);
    fclose(f);

    unlink("TFEA.TMP");
    unlink("TFEB.TMP");
    unlink("TFEC.TMP");

    if (fails)
        printf("tfeof FAILED %d\n", fails);
    else
        printf("tfeof ok\n");
    return fails ? 1 : 0;
}
