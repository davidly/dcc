#include <errno.h>
#include <stdio.h>

int main(void)
{
    static const unsigned modes[] = {
        0x0100U, 0x0101U, 0x0102U, 0xff00U, 0x0003U
    };
    int results[5];
    int errors[5];
    int failures;
    int i;

    failures = 0;
    for (i = 0; i < 5; i++) {
        errno = 0;
        results[i] = setvbuf(stdout, (char *)0, (int)modes[i], 0);
        errors[i] = errno;
        if (results[i] == 0 || errors[i] != EINVAL)
            failures++;
    }

    printf("tsvbmod start\n");
    for (i = 0; i < 5; i++)
        printf("%04x %d %d\n", modes[i], results[i], errors[i]);
    printf("tsvbmod %d/5\n", 5 - failures);

    return failures != 0;
}
