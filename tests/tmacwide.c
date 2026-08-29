/* Macro parameters must not replace the L prefix of a wide literal.
 * Adapted from SDCC's GCC torture execute test 970214-2.c. */
#include <stdio.h>

#define add_wide(L) (L'1' + (L))
#define keep_text(x) "x is not a parameter here"

int main(void)
{
    if (add_wide(0) != L'1' || keep_text(7)[0] != 'x') {
        printf("tmacwide failed\n");
        return 1;
    }

    printf("tmacwide completed with great success\n");
    return 0;
}
