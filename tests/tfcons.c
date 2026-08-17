#include <stdio.h>

extern int read(int fd, void *buf, unsigned count);

static int fails;

static void check(int ok)
{
    if (!ok)
        fails++;
}

int main(void)
{
    char buf[8];
    size_t n;

    buf[0] = 'Z';
    check(fgets(buf, 0, stdin) == 0);
    check(buf[0] == 'Z');
    check(fgets(buf, -1, stdin) == 0);
    check(buf[0] == 'Z');

    check(ungetc('Q', stdin) == 'Q');
    check(read(0, buf, 0) == 0);
    check(fgetc(stdin) == 'Q');
    check(ungetc('P', stdin) == 'P');
    check(read(0, buf, 3) == 3);
    check(buf[0] == 'P' && buf[1] == 'A' && buf[2] == 'B');

    n = fread(buf, 2, 1, stdin);
    check(n == 1);
    check(buf[0] == 'C' && buf[1] == 'D');
    check(gets(buf) == 0);
    check(feof(stdin) != 0);
    clearerr(stdin);
    check(feof(stdin) == 0);

    putchar('\n');
    if (fails) {
        printf("tfcons FAILED %d\n", fails);
        return 1;
    }
    puts("tfcons ok");
    return 0;
}
