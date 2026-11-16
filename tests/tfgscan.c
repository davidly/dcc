/* Cached fgets: record boundaries, exact positions, binary/text EOF and
 * shared pushback. The byte model also checks bytes after the terminator,
 * so a bulk read that consumes or copies beyond the line cannot pass.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>

static unsigned char data[1024];
static char actual[1026];
static char expected[1026];
static int fails;
static int cases;

static void check(int ok)
{
    if (!ok)
        fails++;
}

static void one(FILE *f, long base, int start, int size, int binary, int pb)
{
    int pos = start;
    int n = 0;
    int eof = 0;
    int pending = pb;
    int c;
    char *result;

    cases++;
    check(fseek(f, base + start, SEEK_SET) == 0);
    if (pb >= 0)
        check(ungetc(pb, f) == pb);
    memset(actual, 0x55, sizeof(actual));
    memset(expected, 0x55, sizeof(expected));
    while (n < size - 1) {
        if (pending >= 0) {
            c = pending;
            pending = -1;       /* pushed Ctrl-Z is guaranteed input */
        } else {
            if (pos == sizeof(data)) {
                eof = 1;
                break;
            }
            c = data[pos++];
            if (!binary && c == 0x1a) {
                eof = 1;
                break;
            }
        }
        expected[n++] = c;
        if (c == '\n')
            break;
    }
    if (n || !eof)
        expected[n] = 0;
    result = fgets(actual, size, f);
    check(result == ((n || !eof) ? actual : NULL));
    check(memcmp(actual, expected, sizeof(actual)) == 0);
    check(ftell(f) == base + pos);
    check((feof(f) != 0) == eof);
    check(ferror(f) == 0);

    /* Low-level reads must resume at the first unconsumed byte, including
     * data after a text EOF marker once its indicator has been cleared.
     */
    clearerr(f);
    c = fgetc(f);
    if (pending >= 0)
        check(c == pending);
    else if (pos < sizeof(data))
        check(c == data[pos]);
    else
        check(c == EOF);
}

static void matrix(long base)
{
    static int starts[] = {
        0, 1, 2, 30, 125, 126, 127, 128, 129, 130, 131,
        253, 254, 255, 256, 257, 258, 382, 383, 384,
        509, 510, 511, 512, 513, 514, 639, 640, 641, 1022, 1023, 1024
    };
    static int sizes[] = { 1, 2, 3, 127, 128, 129, 130, 255, 257, 1025 };
    static int pushed[] = { -1, 'P', '\n', 0x1a, 0 };
    FILE *f;
    int binary, i, j, k;

    for (binary = 0; binary < 2; binary++) {
        f = fopen("FGSCAN.TMP", binary ? "rb" : "r");
        check(f != NULL);
        if (!f)
            return;
        for (i = 0; i < sizeof(starts) / sizeof(starts[0]); i++)
            for (j = 0; j < sizeof(sizes) / sizeof(sizes[0]); j++)
                for (k = 0; k < sizeof(pushed) / sizeof(pushed[0]); k++)
                    one(f, base, starts[i], sizes[j], binary, pushed[k]);
        fclose(f);
    }
}

int main(void)
{
    static int newlines[] = { 0, 1, 126, 127, 128, 129, 254, 255, 256,
                              257, 510, 511, 512, 513, 1023 };
    FILE *f, *g;
    int i;

    memset(data, 'a', sizeof(data));
    for (i = 0; i < sizeof(newlines) / sizeof(newlines[0]); i++)
        data[newlines[i]] = '\n';
    data[130] = data[383] = data[640] = data[1022] = 0x1a;
    data[31] = data[280] = data[700] = 0;
    data[90] = '\r';
    f = fopen("FGSCAN.TMP", "wb");
    if (!f)
        return 1;
    check(fwrite(data, 1, sizeof(data), f) == sizeof(data));
    fclose(f);
    matrix(0L);

    /* Evict the shared sector through a different descriptor, then mix
     * line, block and single-byte reads on the original stream.
     */
    f = fopen("FGSCAN.TMP", "rb");
    g = fopen("FGSCAN.TMP", "rb");
    check(f != NULL && g != NULL);
    if (f && g) {
        check(fseek(f, 125L, SEEK_SET) == 0);
        check(fgets(actual, 4, f) == actual);
        check(memcmp(actual, "a\n", 3) == 0);
        check(fseek(g, 600L, SEEK_SET) == 0);
        check(fread(actual, 1, 13, g) == 13);
        check(fgetc(f) == '\n');
        check(fread(actual, 1, 7, f) == 7);
        check(memcmp(actual, data + 128, 7) == 0);
        check(fgets(actual, 130, g) == actual);
        check(ftell(g) == 742L);
    }
    if (f) fclose(f);
    if (g) fclose(g);

    /* A cached record must not survive a write through another stream. */
    f = fopen("FGSCAN.TMP", "rb");
    g = fopen("FGSCAN.TMP", "r+b");
    check(f != NULL && g != NULL);
    if (f && g) {
        check(fgets(actual, 2, f) == actual);
        check(fseek(g, 1L, SEEK_SET) == 0);
        check(fwrite("Z", 1, 1, g) == 1);
        check(fgets(actual, 3, f) == actual);
        check(actual[0] == 'Z' && actual[1] == 'a' && actual[2] == 0);
        check(fseek(g, 1L, SEEK_SET) == 0);
        check(fwrite(data + 1, 1, 1, g) == 1);
    }
    if (f) fclose(f);
    if (g) fclose(g);

    /* Cross the low-word carry while scanning, not only while seeking. */
    f = fopen("FGSCAN.TMP", "r+b");
    check(f != NULL);
    if (f) {
        check(fseek(f, 65408L, SEEK_SET) == 0);
        check(fwrite(data, 1, sizeof(data), f) == sizeof(data));
        fclose(f);
        matrix(65408L);
    }

    /* An unterminated line spanning many complete records and physical EOF. */
    memset(data, 'L', sizeof(data));
    f = fopen("FGSCAN.TMP", "wb");
    check(f != NULL);
    if (f) {
        check(fwrite(data, 1, sizeof(data), f) == sizeof(data));
        fclose(f);
        f = fopen("FGSCAN.TMP", "rb");
        check(f != NULL);
        if (f) {
            one(f, 0L, 0, 1025, 1, -1);
            one(f, 0L, 0, 1026, 1, -1);
            check(fgets(actual, 1, f) == NULL); /* already at EOF */
            fclose(f);
        }
    }

    f = fopen("FGSCAN.TMP", "wb");
    check(f != NULL);
    if (f) {
        actual[0] = 'U';
        errno = 0;
        check(fgets(actual, 0, f) == NULL && actual[0] == 'U');
        check(fgets(actual, -1, f) == NULL && actual[0] == 'U');
        check(fgets(actual, 1, f) == actual && actual[0] == 0);
        check(errno == 0 && !ferror(f));
        actual[0] = 'U';
        check(fgets(actual, 2, (FILE *)((unsigned int)f + 256)) == NULL);
        check(errno == EBADF && actual[0] == 'U' && !ferror(f));
        errno = 0;
        check(fgets(actual, 2, f) == NULL && actual[0] == 'U');
        check(errno == EBADF && ferror(f) && !feof(f));
        check(ftell(f) == 0L);
        fclose(f);
    }
    remove("FGSCAN.TMP");

    if (fails) {
        printf("tfgscan FAILED %d of %d cases\n", fails, cases);
        return 1;
    }
    printf("tfgscan ok (%d cases)\n", cases);
    return 0;
}
