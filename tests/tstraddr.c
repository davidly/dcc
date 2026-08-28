/* File-scope addresses of string-literal elements.
 * Adapted from SDCC's GCC torture execute test 921019-1. */
#include <stdio.h>

static void *addresses[] = {
    (void *)&("XYZ"[0]),
    (void *)&("abc"[1]),
    &"1234"[3]
};

int main(void)
{
    int failures = 0;

    if (((char *)addresses[0])[0] != 'X')
        ++failures;
    if (((char *)addresses[1])[0] != 'b')
        ++failures;
    if (((char *)addresses[2])[0] != '4')
        ++failures;

    if (failures) {
        printf("tstraddr failed: %d\n", failures);
        return 1;
    }
    printf("tstraddr completed with great success\n");
    return 0;
}
