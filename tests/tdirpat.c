/* tdirpat.c - opendir()/readdir() are documented as accepting a wildcard
 * pattern (see 10-system-and-cpm.md), and BDOS 17/18 (the search-first/next
 * calls they're built on) officially support '?' filtering. But dccrtl's
 * _dopn used to ignore the caller's pattern entirely, except for an
 * optional "X:" drive-letter prefix: after that prefix, it unconditionally
 * filled the whole 11-byte name/ext FCB field with '?', so opendir("*.C"),
 * opendir("T?.TMP"), and even opendir("SPECIFIC.TXT") all behaved exactly
 * like opendir("*.*") - full, unfiltered drive enumeration. Fixed by
 * running the pattern through __mkfcb (the same parser unlink()/rename()
 * already use), with an empty/"."/bare-drive-prefix pattern still falling
 * back to full enumeration, matching the documented opendir(".")/
 * opendir("*.*") behavior.
 */
#include <stdio.h>
#include <dirent.h>
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

int main(void)
{
    FILE *f;
    DIR *d;
    struct dirent *e;
    int count = 0, saw_abc = 0, saw_xyz = 0;

    unlink("ZQ1.ABC");
    unlink("ZQ2.XYZ");
    f = fopen("ZQ1.ABC", "w"); fputs("1", f); fclose(f);
    f = fopen("ZQ2.XYZ", "w"); fputs("2", f); fclose(f);

    d = opendir("ZQ*.ABC");
    if (!d) { printf("FAIL opendir\n"); return 1; }
    while ((e = readdir(d)) != NULL) {
        count++;
        if (!strcmp(e->d_name, "ZQ1.ABC")) saw_abc = 1;
        if (!strcmp(e->d_name, "ZQ2.XYZ")) saw_xyz = 1;
    }
    closedir(d);

    chki("pattern_matches_only_one_entry", count, 1);
    chki("matching_file_seen", saw_abc, 1);
    chki("non_matching_file_excluded", saw_xyz, 0);

    unlink("ZQ1.ABC");
    unlink("ZQ2.XYZ");

    if (fails)
        printf("tdirpat FAILED %d\n", fails);
    else
        printf("tdirpat ok\n");
    return fails ? 1 : 0;
}
