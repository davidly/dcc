/* tpadread.c - a partial (non-128-byte-aligned) write into virgin CP/M
 * territory - including the common case of a freshly created/truncated
 * file - pads the rest of that 128-byte record with Ctrl-Z (0x1A) via
 * dccrtl's __zerdm (DCCRTL.MAC), because the record has to be written in
 * full and there's no real "old data" to merge with yet. That padding is
 * genuine, on-disk, readable data: fread()/read() aren't bounded by the
 * C-level tracked length (__fdlen, what ftell()/SEEK_END report) - they
 * just return whatever BDOS's random-read call hands back. So a buffer-
 * sized fread() on a short file can return far more bytes than the file's
 * own reported length, all silently valid as far as fread()'s return count
 * is concerned.
 *
 * This is a real, known limitation - but deliberately NOT "fixed" at the
 * RTL level, and this test still documents the unfixed behavior rather
 * than asserting the theoretically-correct one. An earlier attempt made
 * __rdsz itself trim __fdlen to the exact byte count (scanning the last
 * record for trailing 0x1A, same idea as the append-mode fix in
 * __apseek) and bounded _read() by it. Both parts had to be reverted:
 * a single genuine trailing ^Z byte a program deliberately writes as
 * real text-file-EOF data (the classic CP/M convention - see fileops.c's
 * own cpm_filelen helper, which does exactly this scan itself on top of
 * the *un*trimmed length) is indistinguishable on disk from __zerdm's
 * unwritten-tail padding, so unconditionally trimming __fdlen silently
 * ate real data fileops.c's own test depends on seeing. __apseek's own
 * narrower trim (used only to position "a" mode's append point, never
 * touching the shared __fdlen) is where that idea actually landed safely.
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
    long reported_len;
    int n, i;
    unsigned char buf[64];

    unlink("TPAD.TMP");

    f = fopen("TPAD.TMP", "w");
    if (!f) { printf("FAIL create\n"); return 1; }
    fwrite("AAA", 1, 3, f);
    fclose(f);

    f = fopen("TPAD.TMP", "r");
    if (!f) { printf("FAIL reopen\n"); return 1; }

    fseek(f, 0, SEEK_END);
    reported_len = ftell(f);
    printf("REPORT ftell(SEEK_END) reports: %ld\n", reported_len);
    chki("reported_length_is_exact", (int) reported_len, 3);

    fseek(f, 0, SEEK_SET);
    n = (int) fread(buf, 1, sizeof(buf), f);
    printf("REPORT fread request %u returned: %d bytes\n", (unsigned) sizeof(buf), n);
    chki("fread_bounded_by_reported_length", n, 3);

    if (n > 3) {
        printf("REPORT byte 3 (first byte past the 3 written): 0x%02x\n", buf[3]);
        for (i = 3; i < n && i < 8; i++)
            printf("REPORT buf[%d] = 0x%02x\n", i, buf[i]);
    }

    fclose(f);
    unlink("TPAD.TMP");

    if (fails)
        printf("tpadread FAILED %d\n", fails);
    else
        printf("tpadread ok\n");
    return fails ? 1 : 0;
}
