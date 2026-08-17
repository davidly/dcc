#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fails;

static void fail(const char *msg)
{
    printf("FAIL %s\n", msg);
    fails++;
}

static void check_zero(const char *msg, int rc)
{
    if (rc != 0) {
        printf("FAIL %s: got %d expected 0\n", msg, rc);
        fails++;
    }
}

static void check_nonzero(const char *msg, int rc)
{
    if (rc == 0) {
        printf("FAIL %s: got %d expected nonzero\n", msg, rc);
        fails++;
    }
}

static void check_long(const char *msg, long got, long expected)
{
    if (got != expected) {
        printf("FAIL %s: got %ld expected %ld\n", msg, got, expected);
        fails++;
    }
}

int main(void)
{
    FILE *fp;
    char buf[16];
    int n;

    unlink("TFSEEK.TMP");

    fp = fopen("TFSEEK.TMP", "w+");
    if (!fp) {
        printf("FAIL fopen w+\n");
        return 1;
    }

    if (fputs("abcdef", fp) == EOF) {
        printf("FAIL fputs\n");
        fclose(fp);
        unlink("TFSEEK.TMP");
        return 1;
    }

    check_nonzero("fseek invalid whence", fseek(fp, 0L, 99));
    /* fd 3 is the first real stream and FOPEN_MAX mirrors the slot count. */
    check_nonzero("fseek out-of-range stream",
                  fseek((FILE *)(3 + FOPEN_MAX), 0L, SEEK_SET));

    check_zero("fseek SEEK_SET", fseek(fp, 2L, SEEK_SET));
    check_long("ftell after SEEK_SET", ftell(fp), 2L);

    check_zero("fseek SEEK_CUR", fseek(fp, 1L, SEEK_CUR));
    check_long("ftell after SEEK_CUR", ftell(fp), 3L);

    check_zero("fseek SEEK_END", fseek(fp, 0L, SEEK_END));
    check_long("ftell after SEEK_END", ftell(fp), 6L);

    check_zero("rewind to start", fseek(fp, 0L, SEEK_SET));
    n = (int)fread(buf, 1, 6, fp);
    if (n != 6) {
        printf("FAIL fread count: got %d expected 6\n", n);
        fails++;
    }
    buf[6] = '\0';
    if (strcmp(buf, "abcdef") != 0)
        fail("fread contents after seeks");

    fclose(fp);
    unlink("TFSEEK.TMP");

    if (fails != 0) {
        printf("tfseekr FAILED: %d\n", fails);
        return 1;
    }
    printf("tfseekr: all tests passed\n");
    return 0;
}
