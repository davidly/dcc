/* trenwild.c - rename() (BDOS 23) is one of only three FCB-taking file
 * operations DCCRTL's __mkfcb feeds a possibly-ambiguous FCB to, but unlike
 * delete/search-first (17/18/19), the documented CP/M 2.2 Interface Guide
 * never lists rename as supporting '?' wildcards at all - search and
 * delete are the only functions it says support ambiguous references
 * (see https://www.seasip.info/Cpm/bdos.html).
 *
 * CP/M 2.2's own BDOS.ASM source is looser than the docs promise: rename()
 * technically reuses the same search/searchn loop delete does for the OLD
 * name, and would - undocumented, unsupported - iterate every match. There
 * is no "template" substitution though (an earlier note of mine to that
 * effect was simply wrong): the source just copies the NEW name's bytes
 * verbatim into every matched entry, including any literal '?' in FCB2, so
 * an ambiguous new name would just as happily write garbage '?' bytes into
 * a real directory entry's name.
 *
 * None of that raw-source permissiveness survived into any tested
 * emulator, though: ntvcm, tnylpo, cpmemu, zxcc, and cpm.exe all reject an
 * ambiguous rename outright (nonzero return, nothing renamed) - fully
 * consistent with each other and with the documented (lack of) support.
 * This is correct, expected behavior, not a bug, and isn't affected by the
 * '*'-expansion fix in tstar.c: an ambiguous FCB is still ambiguous
 * whether the '?' came from the caller directly or from '*' expansion.
 */
#include <stdio.h>
#include <unistd.h>

static int fails;

static int exists(const char *name)
{
    FILE *f = fopen(name, "r");
    if (f) { fclose(f); return 1; }
    return 0;
}

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
    int r;

    unlink("RW1.TMP"); unlink("RW2.TMP"); unlink("RW3.TMP");
    unlink("RB1.TMP"); unlink("RB2.TMP"); unlink("RB3.TMP");

    f = fopen("RW1.TMP", "w"); fputs("1", f); fclose(f);
    f = fopen("RW2.TMP", "w"); fputs("2", f); fclose(f);
    f = fopen("RW3.TMP", "w"); fputs("3", f); fclose(f);

    r = rename("RW?.TMP", "RB?.TMP");
    chki("ambiguous_rename_rejected", r != 0, 1);
    chki("rw1_untouched", exists("RW1.TMP"), 1);
    chki("rw2_untouched", exists("RW2.TMP"), 1);
    chki("rw3_untouched", exists("RW3.TMP"), 1);
    chki("rb1_not_created", exists("RB1.TMP"), 0);
    chki("rb2_not_created", exists("RB2.TMP"), 0);
    chki("rb3_not_created", exists("RB3.TMP"), 0);

    unlink("RW1.TMP"); unlink("RW2.TMP"); unlink("RW3.TMP");
    unlink("RB1.TMP"); unlink("RB2.TMP"); unlink("RB3.TMP");

    if (fails)
        printf("trenwild FAILED %d\n", fails);
    else
        printf("trenwild ok\n");
    return fails ? 1 : 0;
}
