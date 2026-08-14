/* tmakewc.c - BDOS 22 (make/create file) never validates the FCB it's
 * given; it just copies it verbatim into an empty directory slot (confirmed
 * against CP/M 2.2's own BDOS.ASM source). So before this fix,
 * fopen("MK?.TMP", "w") - "?" reaching the create path either typed by
 * mistake or from unsanitized input - silently created a real, permanent
 * on-disk file whose name contained that literal '?' byte, on every
 * emulator that let the create through (confirmed on ntvcm and cpmemu; it
 * created a genuine host file literally named "MK?.TMP"). Worse, once
 * created, such a file becomes impossible to clean back up through
 * portable C code: unlink()/opendir() correctly treat '?' as a pattern
 * character, not a literal one, so a wildcard-aware delete/search-first
 * scan will skip right past a real file whose name happens to contain that
 * character (see the corresponding find-first filtering logic on the
 * emulator side).
 *
 * Fixed at the DCCRTL level rather than per-emulator: fopen()/open() now
 * reject a '?' or '*' in the parsed name/ext outright, purely in client
 * code, before any BDOS call is ever made - whenever the call could create
 * a new directory entry (O_CREAT or O_TRUNC, i.e. "w" or "a" mode). This
 * makes the result 100% consistent across every emulator, unlike the
 * read-mode case (see tfopenw.c), since the rejection never depends on
 * what a given BDOS implementation does with an ambiguous FCB at all.
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
    int ghost_found = 0;

    f = fopen("MK?.TMP", "w");
    chki("fopen_w_wildcard_rejected", f == NULL, 1);
    if (f)
        fclose(f);

    /* Nothing should have been created at all - scan the raw directory
     * rather than trusting fopen()'s own (now filtering) opendir/readdir,
     * so a ghost entry can't hide from this check the same way it hid
     * from cleanup before the fix. */
    d = opendir(".");
    while ((e = readdir(d)) != NULL)
        if (!strncmp(e->d_name, "MK", 2))
            ghost_found = 1;
    closedir(d);
    chki("no_ghost_file_created", ghost_found, 0);

    if (fails)
        printf("tmakewc FAILED %d\n", fails);
    else
        printf("tmakewc ok\n");
    return fails ? 1 : 0;
}
