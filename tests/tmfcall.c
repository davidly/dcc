/* Multi-file callback linkage: the callback implementations are retained and
 * linked through an externally defined function-pointer table, not direct
 * calls from this driver. */
#include <stdio.h>
#include "tmfcall.h"

int main(void)
{
    MfCallback *table;
    int first;
    int second;

    table = mf_callback_table;
    first = table[0](10);
    second = mf_dispatch_callback(1, 7);

    if (first != 16 || second != 28) {
        printf("tmfcall failed: %d %d\n", first, second);
        return 1;
    }
    printf("tmfcall completed with great success\n");
    return 0;
}
