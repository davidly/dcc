/* twild.c - dccrtl's __mkfcb copies filename characters verbatim (after
 * uppercasing) with no special-casing for '?' or '*'. CP/M's BDOS treats
 * those bytes in an FCB as wildcards for functions like delete (fn 19) and
 * rename (fn 23), so a literal '?'/'*' that reaches unlink()/rename() -
 * whether typed deliberately or arriving from unsanitized input - can
 * match and act on multiple files in one call instead of the single named
 * file the caller presumably intended. This proves it directly: create
 * three distinct files, unlink() one ambiguous pattern, and report which
 * of the three survive.
 */
#include <stdio.h>

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

    unlink("WA1.TMP");
    unlink("WA2.TMP");
    unlink("WA3.TMP");

    f = fopen("WA1.TMP", "w"); fputs("1", f); fclose(f);
    f = fopen("WA2.TMP", "w"); fputs("2", f); fclose(f);
    f = fopen("WA3.TMP", "w"); fputs("3", f); fclose(f);

    chki("wa1_exists_before", exists("WA1.TMP"), 1);
    chki("wa2_exists_before", exists("WA2.TMP"), 1);
    chki("wa3_exists_before", exists("WA3.TMP"), 1);

    /* Intent: delete only "WA1.TMP". A literal '?' reaching BDOS fn 19
     * unescaped can instead match WA1/WA2/WA3 all at once - or, as ntvcm
     * turned out to do, match nothing and just fail. Neither "deletes
     * extra files" nor "silently does nothing to an ambiguous pattern" is
     * really "correct" for a caller who named one specific file, so this
     * is reported rather than hard-asserted - the interesting part is
     * whether every emulator agrees with ntvcm here or not. */
    r = unlink("WA?.TMP");
    printf("REPORT unlink(\"WA?.TMP\") returned %d\n", r);

    printf("REPORT WA1.TMP exists after: %d\n", exists("WA1.TMP"));
    printf("REPORT WA2.TMP exists after: %d\n", exists("WA2.TMP"));
    printf("REPORT WA3.TMP exists after: %d\n", exists("WA3.TMP"));

    /* The one thing that must hold everywhere: this must not silently
     * corrupt/lose track of files it didn't touch at all. */
    chki("untouched_files_still_intact",
         exists("WA2.TMP") && exists("WA3.TMP"), 1);

    unlink("WA1.TMP");
    unlink("WA2.TMP");
    unlink("WA3.TMP");

    if (fails)
        printf("twild FAILED %d\n", fails);
    else
        printf("twild ok\n");
    return fails ? 1 : 0;
}
