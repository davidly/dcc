#include <errno.h>
#include <stdio.h>
#include <unistd.h>

extern int bdos(int fn, int dearg);

static int fails;

static void check(const char *name, int ok)
{
    if (!ok) {
        printf("FAIL %s errno=%d\n", name, errno);
        fails++;
    }
}

static int exists(const char *name)
{
    FILE *fp = fopen(name, "r");

    if (fp == NULL)
        return 0;
    fclose(fp);
    return 1;
}

int main(void)
{
    FILE *fp;
    char current[13] = "A:R5B.TMP";
    char other[13] = "B:R5C.TMP";
    int drive = bdos(25, 0);

    current[0] = (char)('A' + drive);
    other[0] = current[0] == 'A' ? 'B' : 'A';

    unlink("R5A.TMP");
    unlink(current);
    unlink(other);

    fp = fopen("R5A.TMP", "w");
    check("create", fp != NULL);
    if (fp != NULL) {
        fputs("x", fp);
        fclose(fp);
    }

    errno = 0;
    check("wild old",
          rename("R5?.TMP", "R5B.TMP") == -1 && errno == EINVAL);
    check("wild old preserved", exists("R5A.TMP"));

    errno = 0;
    check("wild new",
          rename("R5A.TMP", "R5?.TMP") == -1 && errno == EINVAL);
    check("wild new preserved", exists("R5A.TMP"));

    errno = 0;
    check("cross drive",
          rename("R5A.TMP", other) == -1 && errno == EXDEV);
    check("cross drive preserved", exists("R5A.TMP"));

    errno = 0;
    check("default current drive", rename("R5A.TMP", current) == 0);
    check("default source gone", !exists("R5A.TMP"));
    check("explicit destination", exists(current));

    unlink("R5A.TMP");
    unlink(current);
    unlink(other);

    if (fails)
        printf("trnb5 FAILED %d\n", fails);
    else
        puts("trnb5 ok");
    return fails ? 1 : 0;
}
