#include <stdio.h>

static int fails;

static void check(int ok)
{
    if (!ok)
        fails++;
}

static void makefile(const char *name, int ch)
{
    FILE *f = fopen(name, "w");
    check(f != 0);
    if (f) {
        fputc(ch, f);
        fclose(f);
    }
}

int main(void)
{
    FILE *a;
    FILE *b;
    FILE *old;
    FILE *r;

    remove("FRIA.TMP");
    remove("FRIB.TMP");
    remove("FRIC.TMP");
    makefile("FRIA.TMP", 'A');
    makefile("FRIB.TMP", 'B');
    makefile("FRIC.TMP", 'C');

    a = fopen("FRIA.TMP", "w");
    b = fopen("FRIB.TMP", "r");
    check(a != 0 && b != 0 && a != b);

    old = b;
    r = b ? freopen("FRIC.TMP", "r", b) : 0;
    check(r == old);
    if (r) {
        check(fgetc(r) == 'C');
        fclose(r);
    }
    if (a) {
        check(fputc('Z', a) == 'Z');
        check(fclose(a) == 0);
    }

    remove("FRIA.TMP");
    remove("FRIB.TMP");
    remove("FRIC.TMP");
    if (fails) {
        printf("tfreid FAILED %d\n", fails);
        return 1;
    }
    puts("tfreid ok");
    return 0;
}
