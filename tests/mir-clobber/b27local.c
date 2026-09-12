#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

static char *make_buf(size_t n)
{
    char *b = (char *)malloc(n);

    if (b == 0) {
        printf("FAIL: malloc(%u) returned NULL\n", (unsigned)n);
        exit(1);
    }
    memset(b, 0, n);
    return b;
}

static void expect_prefix(
    const char *buf, const char *want, const char *tag)
{
    int n = (int)strlen(want);
    int i;

    for (i = 0; i < n; i = i + 1) {
        if (buf[i] != want[i]) {
            g_fail = g_fail + 1;
            printf("FAIL %s: buf[%d]=0x%02x want 0x%02x\n",
                   tag, i, (unsigned char)buf[i],
                   (unsigned char)want[i]);
            return;
        }
    }
    printf("ok: %s adopted (buffered %d bytes)\n", tag, n);
}

extern char *__fastcall bf01(size_t n);
extern void __fastcall bf03(
    const char *buf, const char *want, const char *tag);

#if BUF27_FASTCALL_CLASS == 1
#define BUF27_MAKE_BUF(n) bf01(n)
#else
#define BUF27_MAKE_BUF(n) make_buf(n)
#endif

#if BUF27_FASTCALL_CLASS == 3
#define BUF27_EXPECT_PREFIX(buf, want, tag) \
    bf03((buf), (want), (tag))
#else
#define BUF27_EXPECT_PREFIX(buf, want, tag) \
    expect_prefix((buf), (want), (tag))
#endif

int main(void)
{
    char *big;
    char *line;
    int i;

    printf("tsvbuf2 start\n");

    big = BUF27_MAKE_BUF(4096);
    if (setvbuf(stdout, big, _IOFBF, 4096) != 0)
        printf("FAIL: setvbuf 4K _IOFBF rejected\n");

    printf("4kbuf line A\n");
    printf("4kbuf line B\n");
    printf("4kbuf cost $5 and $10\n");
    BUF27_EXPECT_PREFIX(
        big, "4kbuf line A\r\n4kbuf line B\r\n", "4K-IOFBF");
    fflush(stdout);

    printf("d1: leading $dollar\n");
    printf("d2: trailing dollar$\n");
    printf("d3: $only$dollars$\n");
    printf("d4: $$$$ four in a row\n");
    printf("d5: lone -> $\n");
    printf("$$$$$$$$\n");
    printf("mix: a$b$c$d$e$f\n");
    fflush(stdout);

    {
        char *small = BUF27_MAKE_BUF(32);

        setvbuf(stdout, small, _IOFBF, 32);
        printf(
            "small-buf line definitely longer than thirty-one bytes $x$\n");
        fflush(stdout);
        free(small);
    }

    setvbuf(stdout, big, _IOFBF, 4096);
    line = BUF27_MAKE_BUF(256);
    for (i = 0; i < 200; i = i + 1) {
        line[i] = (char)('0' + (i % 10));
        if (i % 37 == 0)
            line[i] = '$';
    }
    line[200] = '\n';
    line[201] = '\0';
    printf("boundary: %s", line);
    fflush(stdout);
    free(line);

    setvbuf(stdout, big, _IOLBF, 4096);
    fputs("linebuf via fputs: cost is $7$\n", stdout);
    puts("linebuf via puts: $$ and done");

    setvbuf(stdout, (char *)0, _IONBF, 0);
    printf("nobuf: $a$b$ ");
    putchar('X');
    putchar('$');
    putchar('Y');
    putchar('\n');

    {
        char *sbuf = BUF27_MAKE_BUF(BUFSIZ);

        setbuf(stdout, sbuf);
        printf("setbuf-adopt: $a$\n");
        BUF27_EXPECT_PREFIX(sbuf, "setbuf-adopt: $", "setbuf");
        fflush(stdout);
        free(sbuf);
        setbuf(stdout, (char *)0);
        printf("setbuf-null: $unbuffered$\n");
    }

    fprintf(stdout, "fprintf stdout: %d dollars $%d$\n", 3, 100);
    fprintf(stderr, "fprintf stderr: warn $!$\n");

    if (setvbuf(stdout, (char *)0, 7, 0) == 0)
        printf("FAIL: invalid mode 7 accepted\n");
    else
        printf("ok: invalid mode rejected\n");

    {
        FILE *fp;
        char *fbuf;

        memset(big, 0, 4096);
        setvbuf(stdout, big, _IOFBF, 4096);
        printf("file-noop A\n");

        fp = fopen("SVBUF2.TMP", "w");
        if (fp == 0) {
            printf("FAIL: fopen SVBUF2.TMP\n");
        } else {
            fbuf = BUF27_MAKE_BUF(64);
            if (setvbuf(fp, fbuf, _IONBF, 64) != 0)
                printf("FAIL: file setvbuf rejected\n");
            setbuf(fp, fbuf);
            fprintf(fp, "file output $ok$\n");
            fclose(fp);
            free(fbuf);
        }

        printf("file-noop B\n");
        BUF27_EXPECT_PREFIX(
            big, "file-noop A\r\nfile-noop B\r\n", "file-noop");
        fflush(stdout);
        remove("SVBUF2.TMP");
    }

    setvbuf(stdout, (char *)0, _IOLBF, 0);
    free(big);

    if (g_fail == 0)
        printf("tsvbuf2 passed with great success\n");
    else
        printf("tsvbuf2 FAILED (%d)\n", g_fail);

    printf("tsvbuf2 done $tail-no-newline$");
    return 0;
}
