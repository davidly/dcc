/* tappend.c - fopen(path, "a") must position the stream at end-of-file
 * before the first write, per C89 7.9.5.3: "the byte string is appended
 * to the end of the file rather than overwriting the existing contents".
 *
 * dccrtl's _open takes any O_CREAT flag (which "a" maps to, same as "w")
 * straight to its create path, which unconditionally deletes the existing
 * file (BDOS fn 19) and makes a fresh one (BDOS fn 22) - there is no check
 * for "does it already exist and should its content be kept". So "a" mode
 * is not just missing a seek-to-end: it destroys the prior content exactly
 * like "w" does.
 *
 * Reads back an exact byte count (not an oversized buffer) so this result
 * isn't muddied by the separate Ctrl-Z record-padding behavior tpadread.c
 * covers - a partial-record write pads the rest of that record and a
 * bigger read would pick that padding up too.
 */
#include <stdio.h>
#include <string.h>

static int fails;

static void chkstr(const char *name, const char *got, const char *expected)
{
    if (strcmp(got, expected) != 0) {
        printf("FAIL %s: got [%s] expected [%s]\n", name, got, expected);
        fails++;
    } else {
        printf("PASS %s: [%s]\n", name, got);
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

/* The buggy "a" path can leave raw Ctrl-Z record padding in what should be
 * a short, clean ASCII readback (see tpadread.c) - replace anything
 * non-printable with '.' so diagnostic output/baselines stay plain text
 * and comparable across emulators regardless of console-level handling of
 * control bytes. */
static void sanitize(char *buf)
{
    int i;
    for (i = 0; buf[i]; i++)
        if ((unsigned char) buf[i] < 32 || (unsigned char) buf[i] > 126)
            buf[i] = '.';
}

int main(void)
{
    FILE *f;
    char buf[64];

    unlink("TAPP.TMP");

    f = fopen("TAPP.TMP", "w");
    if (!f) { printf("FAIL initial create\n"); return 1; }
    fwrite("1234567890", 1, 10, f);
    fclose(f);

    f = fopen("TAPP.TMP", "a");
    if (!f) { printf("FAIL append open\n"); return 1; }
    fwrite("ABCDE", 1, 5, f);
    fclose(f);

    /* Correct C89 result is the original 10 bytes plus the 5 appended
     * ("1234567890ABCDE", 15 bytes) - read exactly that many. Sanitized
     * before comparing too: the expected string is plain ASCII, so
     * replacing any stray padding byte with '.' can only ever turn a
     * would-be match into a still-correct match, never hide a real one. */
    read_exact("TAPP.TMP", buf, 15);
    sanitize(buf);
    printf("readback after first append: [%s]\n", buf);
    chkstr("append_result", buf, "1234567890ABCDE");

    /* A second append must land after both prior writes, not at 0 again. */
    f = fopen("TAPP.TMP", "a");
    if (!f) { printf("FAIL second append open\n"); return 1; }
    fwrite("Z", 1, 1, f);
    fclose(f);

    read_exact("TAPP.TMP", buf, 16);
    sanitize(buf);
    printf("readback after second append: [%s]\n", buf);
    chkstr("second_append_result", buf, "1234567890ABCDEZ");

    unlink("TAPP.TMP");

    if (fails)
        printf("tappend FAILED %d\n", fails);
    else
        printf("tappend ok\n");
    return fails ? 1 : 0;
}
