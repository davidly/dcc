#include <stdio.h>
#include <string.h>

static int fails;

static void check(const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s got %d expected %d\n", name, got, expected);
        fails++;
    }
}

static FILE *make_file(const char *text)
{
    FILE *fp;

    remove("UGP.TMP");
    fp = fopen("UGP.TMP", "w");
    if (fp == NULL)
        return NULL;
    fputs(text, fp);
    fclose(fp);
    return fopen("UGP.TMP", "r");
}

static void test_bulk_reads(void)
{
    FILE *fp;
    char buf[8];

    fp = make_file("abcdef");
    if (fp == NULL) {
        fails++;
        return;
    }
    check("fread prime", fgetc(fp), 'a');
    check("fread push", ungetc('X', fp), 'X');
    memset(buf, 0, sizeof(buf));
    check("fread count", (int)fread(buf, 1, 4, fp), 4);
    check("fread bytes", memcmp(buf, "Xbcd", 4), 0);
    check("fread next", fgetc(fp), 'e');
    fclose(fp);

    fp = fopen("UGP.TMP", "r");
    check("zero push", ungetc('Q', fp), 'Q');
    check("zero count", (int)fread(buf, 1, 0, fp), 0);
    check("zero preserved", fgetc(fp), 'Q');
    fclose(fp);

    fp = fopen("UGP.TMP", "r");
    while (fgetc(fp) != EOF)
        ;
    check("partial push", ungetc('Z', fp), 'Z');
    buf[0] = 0;
    check("partial item", (int)fread(buf, 2, 1, fp), 0);
    check("partial byte", buf[0], 'Z');
    check("partial eof", feof(fp), 1);
    fclose(fp);

    remove("UGP.TMP");
}

static void test_line_and_scanner_reads(void)
{
    FILE *fp;
    char buf[16];
    int value;

    fp = make_file("abc\n");
    if (fp == NULL) {
        fails++;
        return;
    }
    check("fgets prime", fgetc(fp), 'a');
    check("fgets push", ungetc('A', fp), 'A');
    memset(buf, 0, sizeof(buf));
    check("fgets result", fgets(buf, sizeof(buf), fp) != NULL, 1);
    check("fgets bytes", strcmp(buf, "Abc\n"), 0);
    fclose(fp);

    fp = make_file("x\n");
    check("ctrlz push", ungetc(0x1a, fp), 0x1a);
    memset(buf, 0, sizeof(buf));
    check("ctrlz fgets", fgets(buf, sizeof(buf), fp) != NULL, 1);
    check("ctrlz stored", (unsigned char)buf[0], 0x1a);
    check("ctrlz tail", strcmp(buf + 1, "x\n"), 0);
    fclose(fp);

    fp = make_file("12xy");
    value = 0;
    check("scan fread", fscanf(fp, "%d", &value), 1);
    check("scan value", value, 12);
    memset(buf, 0, sizeof(buf));
    check("scan fread count", (int)fread(buf, 1, 2, fp), 2);
    check("scan fread bytes", memcmp(buf, "xy", 2), 0);
    fclose(fp);

    fp = make_file("34tail\n");
    value = 0;
    check("scan fgets", fscanf(fp, "%d", &value), 1);
    check("scan value two", value, 34);
    memset(buf, 0, sizeof(buf));
    check("scan fgets result", fgets(buf, sizeof(buf), fp) != NULL, 1);
    check("scan fgets bytes", strcmp(buf, "tail\n"), 0);
    fclose(fp);
    remove("UGP.TMP");
}

static void test_console_lines(void)
{
    char buf[16];

    check("stdin fgets push", ungetc('P', stdin), 'P');
    check("stdin fgets", fgets(buf, sizeof(buf), stdin) != NULL, 1);
    check("stdin fgets bytes", strcmp(buf, "Path\n"), 0);
    check("stdin gets push", ungetc('L', stdin), 'L');
    check("stdin gets", gets(buf) != NULL, 1);
    check("stdin gets bytes", strcmp(buf, "Line"), 0);
    check("stdin eof", fgetc(stdin), EOF);
    check("stdin eof set", feof(stdin), 1);
    check("stdin eof push fails", ungetc(EOF, stdin), EOF);
    check("stdin failed keeps eof", feof(stdin), 1);
    check("stdin eof push", ungetc('Q', stdin), 'Q');
    check("stdin push clears eof", feof(stdin), 0);
    check("stdin second fails", ungetc('R', stdin), EOF);
    check("stdin failed keeps clear", feof(stdin), 0);
    check("stdin byte preserved", fgetc(stdin), 'Q');
    putchar('\n');
}

int main(void)
{
    test_bulk_reads();
    test_line_and_scanner_reads();
    test_console_lines();

    if (fails != 0) {
        printf("tugpath FAILED %d\n", fails);
        return 1;
    }
    printf("tugpath ok\n");
    return 0;
}
