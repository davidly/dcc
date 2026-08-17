#include <stdio.h>

static int fails;

static void check(int ok)
{
    if (!ok)
        fails++;
}

int main(void)
{
    FILE *f;
    char buf[8];
    int i;

    remove("FEDGE.TMP");
    f = fopen("FEDGE.TMP", "w");
    check(f != 0);
    if (f) {
        check(fwrite("abcdef", 1, 6, f) == 6);
        fclose(f);
    }

    f = fopen("FEDGE.TMP", "r");
    check(f != 0);
    if (f) {
        buf[0] = 'Z';
        check(fgets(buf, 0, f) == 0 && buf[0] == 'Z');
        check(fgets(buf, -1, f) == 0 && buf[0] == 'Z');
        check(fread(buf, 1, 3, f) == 3);
        check(buf[0] == 'a' && buf[2] == 'c');

        rewind((FILE *)((unsigned int)f + 256));
        check(fread(buf, 1, 1, f) == 1 && buf[0] == 'd');
        rewind((FILE *)200);

        for (i = 0; i < 130; i++)
            fread(buf, 1, 1, f);
        check(feof(f) != 0);
        rewind(f);
        check(feof(f) == 0);
        check(fread(buf, 1, 1, f) == 1 && buf[0] == 'a');
        fclose(f);
    }

    check(fread(buf, 1, 1, (FILE *)99) == 0);
    check(fwrite("x", 1, 1, (FILE *)99) == 0);
    check(fwrite("x", 32768u, 1, (FILE *)99) == 0);

    remove("FEDGE.TMP");
    if (fails) {
        printf("tfedge FAILED %d\n", fails);
        return 1;
    }
    puts("tfedge ok");
    return 0;
}
