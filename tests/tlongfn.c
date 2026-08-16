/* tlongfn.c - two filenames that agree on their first 8 characters silently
 * truncate to the same CP/M 8.3 name in dccrtl's __mkfcb (it copies exactly
 * 8 name characters then discards the rest up to the '.', with no length
 * check and no collision detection). Opening the second name for write
 * therefore deletes/replaces the first file's directory entry without any
 * error, and reading the first name back afterward returns the second
 * file's content. This test proves the collision directly and reports
 * whatever dccrtl/BDOS actually does with it.
 *
 * Reads use an exact byte count rather than an oversized buffer - see
 * tpadread.c for why a bigger read would pick up trailing Ctrl-Z record
 * padding instead of cleanly showing the real 3-byte content.
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

    unlink("VERYLONGFILE1.TXT");
    unlink("VERYLONGFILE2.TXT");
    unlink("VERYLONG.TXT"); /* the truncated name both alias to */

    f = fopen("VERYLONGFILE1.TXT", "w");
    if (!f) { printf("FAIL create file1\n"); return 1; }
    fputs("AAA", f);
    fclose(f);

    /* Sanity: right after creating it, file1's own name must read back. */
    read_exact("VERYLONGFILE1.TXT", buf, 3);
    printf("REPORT file1 immediately after creation: [%s]\n", buf);
    chki("file1_initial_content", strcmp(buf, "AAA") == 0, 1);

    f = fopen("VERYLONGFILE2.TXT", "w");
    if (!f) { printf("FAIL create file2\n"); return 1; }
    fputs("BBB", f);
    fclose(f);

    /* The interesting question: does file1's name still open, and if so,
     * with which content? A truncation collision means this now reads
     * file2's data ("BBB"), not file1's original data ("AAA"). */
    read_exact("VERYLONGFILE1.TXT", buf, 3);
    printf("REPORT file1 name after file2 created: [%s]\n", buf);
    chki("truncation_collision_observed", strcmp(buf, "BBB") == 0, 1);

    /* And the underlying truncated name directly. */
    read_exact("VERYLONG.TXT", buf, 3);
    printf("REPORT VERYLONG.TXT direct content: [%s]\n", buf);

    unlink("VERYLONGFILE1.TXT");
    unlink("VERYLONGFILE2.TXT");
    unlink("VERYLONG.TXT");

    if (fails)
        printf("tlongfn FAILED %d\n", fails);
    else
        printf("tlongfn ok\n");
    return fails ? 1 : 0;
}
