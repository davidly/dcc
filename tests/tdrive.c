/* tdrive.c - a "A:" (current-drive) prefix on a path should be equivalent
 * to no prefix at all: dccrtl's __mkfcb hardcodes the FCB drive byte to 0
 * (default drive) and has no code path that recognizes or strips a
 * "letter:" prefix from the filename string at all, so a prefixed path is
 * copied byte-for-byte (including the colon) into the 8-character FCB name
 * field instead. That makes "A:NAME.EXT" address a *different*,
 * oddly-named file than "NAME.EXT", not the same file on drive A. This
 * test proves it directly: write through one form, read back through the
 * other, and show whether they see the same bytes.
 *
 * Reads use an exact byte count rather than an oversized buffer - see
 * tpadread.c for why a bigger read would pick up trailing Ctrl-Z record
 * padding instead of cleanly showing "not found"/wrong content.
 */
#include <stdio.h>
#include <string.h>
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

static void read_exact(const char *path, char *buf, int want)
{
    FILE *f = fopen(path, "r");
    int n;
    if (!f) { strcpy(buf, "<open failed>"); return; }
    n = (int) fread(buf, 1, want, f);
    buf[n] = 0;
    fclose(f);
}

int main(void)
{
    FILE *f;
    char buf[32];

    unlink("TDRV.TMP");

    /* Write through the unprefixed name. */
    f = fopen("TDRV.TMP", "w");
    if (!f) { printf("FAIL create TDRV.TMP\n"); return 1; }
    fputs("noprefix", f);
    fclose(f);

    /* Read back through an "A:" (current drive) prefixed name - should be
     * the identical file if the prefix is parsed and stripped correctly. */
    read_exact("A:TDRV.TMP", buf, 8);
    printf("REPORT A:TDRV.TMP read back: [%s]\n", buf);
    chki("a_prefix_opens_same_file", strcmp(buf, "noprefix") == 0, 1);

    unlink("TDRV.TMP");
    unlink("A:TDRV.TMP"); /* in case the buggy path actually created a distinct file */

    /* Now the other direction: create through the prefixed name, read back
     * through the unprefixed name. */
    f = fopen("A:TDRV2.TMP", "w");
    if (!f) {
        printf("REPORT fopen(\"A:TDRV2.TMP\", \"w\") failed outright\n");
    } else {
        fputs("viaprefix", f);
        fclose(f);

        read_exact("TDRV2.TMP", buf, 9);
        printf("REPORT TDRV2.TMP read back: [%s]\n", buf);
        chki("unprefixed_sees_prefixed_create", strcmp(buf, "viaprefix") == 0, 1);
    }

    unlink("TDRV2.TMP");
    unlink("A:TDRV2.TMP");

    if (fails)
        printf("tdrive FAILED %d\n", fails);
    else
        printf("tdrive ok\n");
    return fails ? 1 : 0;
}
