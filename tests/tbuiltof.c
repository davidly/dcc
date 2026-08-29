/* GCC/SDCC builtin offsetof spelling, adapted from GCC torture pr30778.c. */
#include <stdio.h>
#include <string.h>

struct Record {
    void *first;
    int count;
    char flag;
    long sentinel;
};

int main(void)
{
    struct Record record;

    record.first = (void *)1;
    record.count = 2;
    record.flag = 3;
    record.sentinel = 0x12345678L;
    memset(&record, 0, __builtin_offsetof(struct Record, sentinel));

    if (record.first != 0 || record.count != 0 || record.flag != 0 ||
        record.sentinel != 0x12345678L) {
        printf("tbuiltof failed\n");
        return 1;
    }

    printf("tbuiltof completed with great success\n");
    return 0;
}
