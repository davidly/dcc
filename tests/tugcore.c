#include <stdio.h>

extern int close(int fd);
extern long lseek(int fd, long offset, int whence);

static int fails;

static void check(const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s got %d expected %d\n", name, got, expected);
        fails++;
    }
}

static FILE *make_file(const char *name, const char *text)
{
    FILE *fp;

    remove(name);
    fp = fopen(name, "w");
    if (fp == NULL)
        return NULL;
    fputs(text, fp);
    fclose(fp);
    return fopen(name, "r");
}

static void test_full_and_eof(void)
{
    FILE *fp;
    int ch;

    fp = make_file("UGC1.TMP", "abc");
    if (fp == NULL) {
        fails++;
        return;
    }

    check("first push", ungetc('X', fp), 'X');
    check("second fails", ungetc('Y', fp), EOF);
    check("first preserved", fgetc(fp), 'X');
    check("file preserved", fgetc(fp), 'a');

    while ((ch = fgetc(fp)) != EOF)
        ;
    check("eof set", feof(fp), 1);
    check("eof arg fails", ungetc(EOF, fp), EOF);
    check("failed keeps eof", feof(fp), 1);
    check("push at eof", ungetc('Z', fp), 'Z');
    check("push clears eof", feof(fp), 0);
    check("eof byte", fgetc(fp), 'Z');
    check("eof again", fgetc(fp), EOF);
    check("eof reset", feof(fp), 1);

    fclose(fp);
    remove("UGC1.TMP");
}

static void test_positioning(void)
{
    FILE *fp;

    fp = make_file("UGC2.TMP", "123");
    if (fp == NULL) {
        fails++;
        return;
    }

    check("seek prime", fgetc(fp), '1');
    check("seek push", ungetc('X', fp), 'X');
    check("failed lseek", (int)lseek((int)fp, 0L, 99), -1);
    check("failed preserves", fgetc(fp), 'X');
    check("lseek push", ungetc('Y', fp), 'Y');
    check("lseek ok", (int)lseek((int)fp, 0L, SEEK_SET), 0);
    check("lseek clears", fgetc(fp), '1');
    check("fseek push", ungetc('Z', fp), 'Z');
    check("fseek ok", fseek(fp, 1L, SEEK_SET), 0);
    check("fseek clears", fgetc(fp), '2');
    check("rewind push", ungetc('W', fp), 'W');
    rewind(fp);
    check("rewind clears", fgetc(fp), '1');

    fclose(fp);
    remove("UGC2.TMP");
}

static void test_open_close_reuse(void)
{
    FILE *fp;
    int fd;

    remove("UGC3.TMP");
    fp = fopen("UGC3.TMP", "w");
    if (fp == NULL) {
        fails++;
        return;
    }
    fputs("new", fp);
    fclose(fp);

    check("stale free push", ungetc('S', (FILE *)3), 'S');
    fp = fopen("UGC3.TMP", "r");
    if (fp == NULL) {
        fails++;
        return;
    }
    check("fopen clears", fgetc(fp), 'n');
    check("close push", ungetc('C', fp), 'C');
    fd = (int)fp;
    check("raw close", close(fd), 0);
    check("close clears", fgetc((FILE *)fd), EOF);

    fp = fopen("UGC3.TMP", "r");
    if (fp == NULL) {
        fails++;
        return;
    }
    check("reuse clean", fgetc(fp), 'n');
    fclose(fp);
    remove("UGC3.TMP");
}

static void test_wide_stream(void)
{
    FILE *fp;
    char buf[4];

    fp = make_file("UGC4.TMP", "ab");
    if (fp == NULL) {
        fails++;
        return;
    }
    check("wide test fd", (int)fp, 3);

    check("wide get push", ungetc('X', fp), 'X');
    check("wide get fails", fgetc((FILE *)0x0103), EOF);
    check("wide get preserves", fgetc(fp), 'X');

    check("wide put push", ungetc('Y', fp), 'Y');
    check("wide put fails", ungetc('Z', (FILE *)0x0103), EOF);
    check("wide put preserves", fgetc(fp), 'Y');

    check("wide gets push", ungetc('Q', fp), 'Q');
    check("wide gets fails", fgets(buf, sizeof(buf), (FILE *)0x0103) == NULL, 1);
    check("wide gets preserves", fgetc(fp), 'Q');

    fclose(fp);
    remove("UGC4.TMP");
}

int main(void)
{
    test_full_and_eof();
    test_positioning();
    test_open_close_reuse();
    test_wide_stream();

    if (fails != 0) {
        printf("tugcore FAILED %d\n", fails);
        return 1;
    }
    printf("tugcore ok\n");
    return 0;
}
