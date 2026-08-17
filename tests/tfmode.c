#include <stdio.h>

static int fails;

static void check(int ok)
{
    if (!ok)
        fails++;
}

static void badmode(const char *mode)
{
    FILE *f = fopen("FMODE.TMP", mode);
    check(f == 0);
    if (f)
        fclose(f);
}

int main(void)
{
    FILE *f;
    char data[4];
    char buf[8];

    remove("FMODE.TMP");
    data[0] = 'A';
    data[1] = 0x1a;
    data[2] = 'B';
    data[3] = '\n';

    f = fopen("FMODE.TMP", "wb");
    check(f != 0);
    if (f) {
        check(fwrite(data, 1, 4, f) == 4);
        fclose(f);
    }

    f = fopen("FMODE.TMP", "rb");
    check(f != 0);
    if (f) {
        check(fgets(buf, sizeof(buf), f) == buf);
        check(buf[0] == 'A');
        check((unsigned char)buf[1] == 0x1a);
        check(buf[2] == 'B' && buf[3] == '\n' && buf[4] == 0);
        fclose(f);
    }

    f = fopen("FMODE.TMP", "r+b");
    check(f != 0);
    if (f)
        fclose(f);
    f = fopen("FMODE.TMP", "rb+");
    check(f != 0);
    if (f)
        fclose(f);

    f = fopen("FMODE.TMP", "r");
    check(f != 0);
    if (f) {
        check(fgets(buf, sizeof(buf), f) == buf);
        check(buf[0] == 'A' && buf[1] == 0);
        fclose(f);
    }

    f = fopen("FMODE.TMP", "ab");
    check(f != 0);
    if (f) {
        check(fseek(f, 0L, SEEK_SET) == 0);
        check(fwrite("C", 1, 1, f) == 1);
        check(fseek(f, 0L, SEEK_SET) == 0);
        check(fwrite("D", 1, 1, f) == 1);
        fclose(f);
    }
    f = fopen("FMODE.TMP", "rb");
    check(f != 0);
    if (f) {
        check(fread(buf, 1, 6, f) == 6);
        check(buf[0] == 'A' && (unsigned char)buf[1] == 0x1a);
        check(buf[2] == 'B' && buf[3] == '\n');
        check(buf[4] == 'C' && buf[5] == 'D');
        fclose(f);
    }

    badmode("");
    badmode("q");
    badmode("rr");
    badmode("rbb");
    badmode("r++");
    badmode("rbt");
    badmode("br");

    remove("FMODE.TMP");
    if (fails) {
        printf("tfmode FAILED %d\n", fails);
        return 1;
    }
    puts("tfmode ok");
    return 0;
}
