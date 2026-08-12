/* tstar.c - unlike '?', a literal '*' is not a BDOS-level wildcard at all.
 * CP/M 2.2's BDOS only ever treats '?' specially (see twild.c); '*' has no
 * meaning to BDOS itself - the shell-glob-style "*.C matches every .C file"
 * convenience is implemented by the CCP's own command-line parser (and by
 * virtually every historic CP/M C runtime library), which expands a '*' by
 * filling the rest of the current field with '?' before the FCB ever
 * reaches BDOS. dccrtl's __mkfcb now does the same: a '*' in the name or
 * extension fills the remainder of that field with '?'.
 *
 * Before this fix, __mkfcb copied a literal '*' byte straight into the FCB
 * unchanged. Since no real on-disk filename ever contains a literal '*'
 * byte, unlink("SA*.TMP") silently matched nothing and failed - confirmed
 * identically on ntvcm, tnylpo, and cpmemu. cpm.exe was the one outlier:
 * it actually deleted the matching files, almost certainly because it
 * resolves the pattern through a host Windows API that natively globs '*'
 * - meaning a C program relying on that would have silently worked on one
 * platform and nowhere else. Expanding '*' in __mkfcb itself makes the
 * behavior consistent everywhere DCCRTL runs, independent of whatever
 * wildcard conventions the underlying host/emulator does or doesn't have.
 *
 * Note: cpmemu turns out not to implement wildcard delete via BDOS fn 19
 * at all - not even the single-'?' case in twild.c passes there. That's a
 * pre-existing cpmemu limitation unrelated to this fix (nothing in DCCRTL
 * can work around another program's BDOS not honoring '?' the way the
 * spec requires), so it isn't asserted here; see the README's emulator
 * table for details.
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

    unlink("SA1.TMP");
    unlink("SA2.TMP");

    f = fopen("SA1.TMP", "w"); fputs("1", f); fclose(f);
    f = fopen("SA2.TMP", "w"); fputs("2", f); fclose(f);

    chki("sa1_exists_before", exists("SA1.TMP"), 1);
    chki("sa2_exists_before", exists("SA2.TMP"), 1);

    r = unlink("SA*.TMP");
    chki("unlink_star_pattern", r, 0);
    chki("sa1_deleted", exists("SA1.TMP"), 0);
    chki("sa2_deleted", exists("SA2.TMP"), 0);

    unlink("SA1.TMP");
    unlink("SA2.TMP");

    if (fails)
        printf("tstar FAILED %d\n", fails);
    else
        printf("tstar ok\n");
    return fails ? 1 : 0;
}
