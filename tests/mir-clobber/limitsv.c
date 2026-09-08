#include <stdio.h>
#include <limits.h>

int main(void)
{
    int passed = 0;
    int total = 0;
    printf("Starting C89 Limits Validation...\n\n");
    total++;
    printf("Test 1: char (8-bit) limits... ");
    if (CHAR_BIT == 8 && SCHAR_MIN == -128 && SCHAR_MAX == 127) {
        if ((unsigned char)(UCHAR_MAX + 1) == 0) {
            printf("PASSED\n");
            passed++;
        } else printf("FAILED (Math mismatch)\n");
    } else printf("FAILED (Constants mismatch)\n");
    total++;
    printf("Test 2: 2-byte (16-bit) short limits... ");
    if (SHRT_MIN == -32768 && SHRT_MAX == 32767) {
        if ((unsigned short)(USHRT_MAX + 1) == 0) {
            printf("PASSED\n");
            passed++;
        } else printf("FAILED (Math mismatch)\n");
    } else printf("FAILED (Constants mismatch)\n");
    total++;
    printf("Test 3: 4-byte (32-bit) long limits... ");
    if (LONG_MIN == -2147483647L - 1L && LONG_MAX == 2147483647L) {
        if ((unsigned long)(ULONG_MAX + 1L) == 0) {
            printf("PASSED\n");
            passed++;
        } else printf("FAILED (Math mismatch)\n");
    } else printf("FAILED (Constants mismatch)\n");
    printf("limits extra control\n");
    printf("\nResults: %d/%d tests passed.\n", passed, total);
    return (passed == total) ? 0 : 1;
}
