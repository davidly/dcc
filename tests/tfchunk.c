#include <stdio.h>

static unsigned char data[32768];
static int fails;

static unsigned char pattern(unsigned int i)
{
    return (unsigned char)(i * 37u + (i >> 8));
}

static void check(int ok)
{
    if (!ok)
        fails++;
}

int main(void)
{
    FILE *fp;
    unsigned int i;

    remove("FCHUNK.TMP");
    for (i = 0; i < 32768u; i++)
        data[i] = pattern(i);

    fp = fopen("FCHUNK.TMP", "w+b");
    check(fp != NULL);
    if (fp != NULL) {
        check(fwrite(data, 16384u, 2, fp) == 2);
        check(ftell(fp) == 32768L);
        check(fseek(fp, 0L, SEEK_SET) == 0);

        for (i = 0; i < 32768u; i++)
            data[i] = 0;

        check(fread(data, 16384u, 2, fp) == 2);
        check(ftell(fp) == 32768L);
        check(ferror(fp) == 0);
        for (i = 0; i < 32768u; i++)
            check(data[i] == pattern(i));
        fclose(fp);
    }
    remove("FCHUNK.TMP");

    if (fails) {
        printf("tfchunk FAILED %d\n", fails);
        return 1;
    }
    puts("tfchunk ok");
    return 0;
}
