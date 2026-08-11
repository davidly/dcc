/* trenamex.c - dccrtl's rename() (BDOS fn 23) never checks whether the
 * destination name already exists before renaming onto it. Real CP/M BDOS
 * doesn't guard this either: renaming onto an existing name can leave two
 * directory entries sharing one name, an ambiguous state where later
 * opens/deletes of that name may hit either file. This exercises exactly
 * that case and reports what actually comes back - which file's content
 * "NEWNAME.TMP" reads as afterward, whether the rename call itself reports
 * success, and whether the source name still exists too.
 *
 * Reads use an exact byte count rather than an oversized buffer - see
 * tpadread.c for why a bigger read would pick up trailing Ctrl-Z record
 * padding instead of cleanly showing the real content.
 */
#include <stdio.h>
#include <string.h>

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
    printf("REPORT rename() onto existing destination returned %d\n", r);

    printf("REPORT RXOLD.TMP still exists: %d\n", exists("RXOLD.TMP"));

    read_exact("RXNEW.TMP", buf, 11);
    printf("REPORT RXNEW.TMP content after rename: [%s]\n", buf);

    /* Whatever the outcome, the runtime must not have corrupted the
     * directory so badly that neither name is usable afterward - confirm
     * at least one of RXOLD.TMP/RXNEW.TMP is still readable. */
    chki("at_least_one_name_survives",
         exists("RXOLD.TMP") || exists("RXNEW.TMP"), 1);

    unlink("RXOLD.TMP");
    unlink("RXNEW.TMP");

    if (fails)
        printf("trenamex FAILED %d\n", fails);
    else
        printf("trenamex ok\n");
    return fails ? 1 : 0;
}
