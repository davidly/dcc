/*
 * tbfdate.c - adjacent anonymous bitfield-store regression.
 * Generated archive case: batch8/c1122.
 */
#include <stdio.h>

struct PackedDate {
    union {
        unsigned short raw;
        struct {
            unsigned day : 5;
            unsigned month : 4;
            unsigned year : 7;
        };
    };
};

int main(void)
{
    struct PackedDate date;
    int ok;

    date.raw = 0;
    date.year = 26;
    date.month = 7;
    date.day = 10;
    printf("tbfdate date=%u,%u,%u\n",
           date.year, date.month, date.day);
    ok = date.year == 26 && date.month == 7 && date.day == 10;
    if (ok)
        printf("tbfdate passed with great success\n");
    return !ok;
}
