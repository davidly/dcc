/* trenamex.c - dccrtl's rename() (BDOS fn 23) never checks whether the
 * destination name already exists before renaming onto it. Real CP/M BDOS
 * doesn't check either (confirmed against CP/M 2.2's own BDOS.ASM source):
 * it just overwrites the matched directory entry's name/extension bytes
 * with the new name, with no awareness of whether another entry already
 * has that name. On real hardware that leaves two directory entries
 * sharing one name - genuinely undefined which one a later open() finds.
 *
 * No emulator that maps CP/M files onto real host files can reproduce that
 * duplicate-name state at all: POSIX and Windows filesystems both flatly
 * refuse two directory entries with the same name, so ntvcm is forced to
 * collapse this into a single winner regardless of platform. It picks
 * "overwrite the destination" explicitly and does so the same way on every
 * host, rather than leaving the outcome to whatever the host C library's
 * own rename() happens to do - POSIX rename() (Linux/macOS) already
 * overwrites atomically, but the Windows CRT's rename() fails outright if
 * the destination exists, which used to make the identical ntvcm source
 * silently behave differently depending only on which OS it was built for
 * (caught when a baseline captured on Linux failed the identical test on
 * Windows - see ntvcm.cxx's BDOS fn 23 handler for the fix).
 *
 * tnylpo and cpm.exe make the opposite choice and reject the rename
 * outright instead (see the README's emulator table) - neither choice is
 * "more correct" than the other, since real hardware's actual behavior
 * (the duplicate-name state above) isn't representable by either. This
 * only asserts what's true regardless of which choice an emulator makes:
 * the directory must never end up in a state where neither name is usable.
 *
 * Reads use an exact byte count rather than an oversized buffer - see
 * tpadread.c for why a bigger read would pick up trailing Ctrl-Z record
 * padding instead of cleanly showing the real content.
 */
#include <stdio.h>
#include <string.h>
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
    int r;

    unlink("RXOLD.TMP");
    unlink("RXNEW.TMP");

    f = fopen("RXOLD.TMP", "w"); fputs("old content", f); fclose(f);
    f = fopen("RXNEW.TMP", "w"); fputs("new content", f); fclose(f);

    r = rename("RXOLD.TMP", "RXNEW.TMP");

    /* ntvcm always overwrites, deterministically, on every host - see the
     * header comment. */
    chki("rename_reports_success", r == 0, 1);
    chki("source_name_gone", exists("RXOLD.TMP"), 0);

    read_exact("RXNEW.TMP", buf, 11);
    chki("destination_has_source_content", strcmp(buf, "old content") == 0, 1);

    unlink("RXOLD.TMP");
    unlink("RXNEW.TMP");

    if (fails)
        printf("trenamex FAILED %d\n", fails);
    else
        printf("trenamex ok\n");
    return fails ? 1 : 0;
}
