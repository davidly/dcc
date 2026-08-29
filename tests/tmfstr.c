/* Multi-file aggregate ABI: return a mixed-width struct from one translation
 * unit, mutate it through a pointer, and pass the complete value to another. */
#include <stdio.h>
#include "tmfstr.h"

int main(void)
{
    struct MfRecord record;
    int result;

    record = mf_make_record(12, 70000L, 'A');
    mf_adjust_record(&record, 3);
    result = mf_consume_record(record);

    if (record.count != 15 || record.total != 70003L ||
        record.tag != 'B' || result != 84) {
        printf("tmfstr failed: %d %ld %c %d\n",
               record.count, record.total, record.tag, result);
        return 1;
    }
    printf("tmfstr completed with great success\n");
    return 0;
}
