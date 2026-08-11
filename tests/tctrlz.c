/* tctrlz.c - an embedded (non-trailing) 0x1A (Ctrl-Z) byte is the classic
 * CP/M text-file EOF marker, but only fgets() in dccrtl treats it that way;
 * fread() and fgetc() are binary-safe and don't special-case it at all. This
 * pins down that asymmetry: fgets() must stop exactly at the embedded 0x1A
 * (and feof() must then read true, even though real data follows it in the
 * file), while fread() and fgetc() must see every byte, including the 0x1A
 * itself and everything after it.
 *
 * fread()/fgetc() checks below look at specific byte positions rather than
 * an exact total count: the fixture is under 128 bytes, so the write pads
 * the rest of its record with more Ctrl-Z (see tpadread.c) and a
 * binary-safe reader correctly keeps going into that padding too - that's
 * tpadread.c's finding, not this one, so it's deliberately not asserted
 * here as an exact byte count.
 */
#include <stdio.h>
#include <string.h>

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

static void chkstr(const char *name, const char *got, const char *expected)
{
    if (strcmp(got, expected) != 0) {
        printf("FAIL %s: got [%s] expected [%s]\n", name, got, expected);
        fails++;
    } else {
        printf("PASS %s: [%s]\n", name, got);
    }
}

/* "line1\n" + 0x1A (embedded, NOT the last byte) + "line2\n" + "line3\n" */
static void make_fixture(void)
{
    FILE *f = fopen("TCTZ.TMP", "wb");
    fputs("line1\n", f);
    fputc(0x1a, f);
    fputs("line2\n", f);
    fputs("line3\n", f);
    fclose(f);
}

int main(void)
{
    FILE *f;
    char buf[32];
    int c, n, total;

    make_fixture();

    /* --- fgets: must stop right after reading up to the embedded 0x1A --- */
    f = fopen("TCTZ.TMP", "r");
    if (!f) { printf("FAIL fgets open\n"); return 1; }

    if (!fgets(buf, sizeof buf, f)) { printf("FAIL fgets line1\n"); fails++; }
    else chkstr("fgets_line1", buf, "line1\n");

    /* Second fgets call hits the embedded 0x1A immediately: dccrtl's
     * documented behavior is to stop there and report EOF, discarding
     * "line2"/"line3" even though they're real trailing data. */
    c = (fgets(buf, sizeof buf, f) != NULL);
    chki("fgets_hits_ctrlz_returns_null", c, 0);
    chki("feof_after_ctrlz", feof(f) != 0, 1);

    fclose(f);

    /* --- fread: fully binary-safe, must return at least every real byte,
     * not stop early at the embedded 0x1A. "line1\n" (6) + 0x1a (1) +
     * "line2\n" (6) + "line3\n" (6) = 19 real bytes minimum. */
    f = fopen("TCTZ.TMP", "rb");
    if (!f) { printf("FAIL fread open\n"); return 1; }
    total = (int) fread(buf, 1, sizeof buf, f);
    fclose(f);

    printf("REPORT fread total bytes (>=19 expected; more is the separate\n"
           "  tpadread.c record-padding finding, not a fread problem): %d\n", total);
    chki("fread_reaches_at_least_19_bytes", total >= 19, 1);
    chki("fread_sees_ctrlz_byte", (unsigned char) buf[6], 0x1a);
    chki("fread_sees_bytes_after_ctrlz", (unsigned char) buf[7], 'l');
    chki("fread_sees_line3", (unsigned char) buf[18], '\n');

    /* --- fgetc: also binary-safe, one byte at a time --- */
    f = fopen("TCTZ.TMP", "rb");
    if (!f) { printf("FAIL fgetc open\n"); return 1; }
    n = 0;
    while ((c = fgetc(f)) != EOF) {
        if (n == 6)
            chki("fgetc_sees_ctrlz", c, 0x1a);
        if (n == 18)
            chki("fgetc_sees_line3_end", c, '\n');
        n++;
        if (n > 200) break; /* safety: never spin forever on a runaway read */
    }
    fclose(f);
    printf("REPORT fgetc total bytes (>=19 expected, same caveat as fread): %d\n", n);
    chki("fgetc_reaches_at_least_19_bytes", n >= 19, 1);

    unlink("TCTZ.TMP");

    if (fails)
        printf("tctrlz FAILED %d\n", fails);
    else
        printf("tctrlz ok\n");
    return fails ? 1 : 0;
}
