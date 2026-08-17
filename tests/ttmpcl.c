#include <stdio.h>
#include <string.h>

static int fails;

static void check(int ok)
{
    if (!ok)
        fails++;
}

int main(void)
{
    FILE *f;
    FILE *tf;
    char name[L_tmpnam];
    char byte;
    int i;

    remove("T000.TMP");
    remove("T001.TMP");
    remove("T002.TMP");
    remove("STALE.TMP");

    f = fopen("T000.TMP", "w");
    check(f != 0);
    if (f)
        fclose(f);

    check(tmpnam(name) == name);
    check(strcmp(name, "T000.TMP") != 0);
    f = fopen(name, "r");
    check(f == 0);
    if (f)
        fclose(f);

    f = fopen("STALE.TMP", "w");
    check(f != 0);
    if (f) {
        fputc('x', f);
        fclose(f);
    }
    f = fopen("STALE.TMP", "r");
    check(f != 0);
    if (f) {
        for (i = 0; i < 130; i++)
            fread(&byte, 1, 1, f);
        check(feof(f) != 0);
        fclose(f);
    }

    tf = tmpfile();
    check(tf != 0);
    if (tf) {
        check(feof(tf) == 0);
        check(ferror(tf) == 0);
        fputs("ok\n", tf);
        rewind(tf);
        check(fgets(name, sizeof(name), tf) == name);
        check(strcmp(name, "ok\n") == 0);
        check(fclose(tf) == 0);
    }

    f = fopen("T002.TMP", "r");
    check(f == 0);
    if (f)
        fclose(f);

    remove("T000.TMP");
    remove("T001.TMP");
    remove("T002.TMP");
    remove("STALE.TMP");
    if (fails) {
        printf("ttmpcl FAILED %d\n", fails);
        return 1;
    }
    puts("ttmpcl ok");
    return 0;
}
