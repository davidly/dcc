/*
 * tbfprot.c - mixed-width anonymous bitfield-store regression.
 * Generated archive case: batch8/c1128.
 */
#include <stdio.h>

struct Protocol {
    union {
        unsigned short raw;
        struct {
            unsigned version : 3;
            unsigned kind : 5;
            unsigned length : 8;
        };
    };
};

int main(void)
{
    struct Protocol protocol;
    int ok;

    protocol.raw = 0;
    protocol.version = 2;
    protocol.kind = 17;
    protocol.length = 64;
    printf("tbfprot protocol=%u,%u,%u\n",
           protocol.version, protocol.kind, protocol.length);
    ok = protocol.version == 2 &&
         protocol.kind == 17 && protocol.length == 64;
    if (ok)
        printf("tbfprot passed with great success\n");
    return !ok;
}
