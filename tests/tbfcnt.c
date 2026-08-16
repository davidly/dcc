/*
 * tbfcnt.c - pointer-based bitfield-store address regression.
 * Generated archive case: batch8/c1131.
 */
#include <stdio.h>

struct Counter {
    union {
        unsigned short raw;
        struct {
            unsigned value : 12;
            unsigned overflow : 1;
            unsigned reserved : 3;
        };
    };
};

static void increment(struct Counter *counter)
{
    if (counter->value == 4095) {
        counter->value = 0;
        counter->overflow = 1;
    } else {
        ++counter->value;
    }
}

int main(void)
{
    struct Counter counter;
    int ok;

    counter.raw = 0;
    counter.value = 4095;
    increment(&counter);
    printf("tbfcnt counter=%u,%u\n",
           counter.value, counter.overflow);
    ok = counter.value == 0 && counter.overflow == 1;
    if (ok)
        printf("tbfcnt passed with great success\n");
    return !ok;
}
