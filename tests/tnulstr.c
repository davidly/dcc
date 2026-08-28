/* Embedded NUL bytes must not truncate a global string initializer. */
#include <stdio.h>

static const unsigned char bytes[] = "\0\377";

int main(void)
{
    if (sizeof(bytes) != 3 || bytes[0] != 0 ||
        bytes[1] != 255 || bytes[2] != 0) {
        printf("tnulstr failed\n");
        return 1;
    }
    printf("tnulstr completed with great success\n");
    return 0;
}
