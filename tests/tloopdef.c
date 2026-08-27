/*
 * tloopdef.c - regression coverage for object-value promotion at a loop
 * header.  A local initialized only on the first iteration must remain a
 * memory load on the second; the entry edge is reachable with that object
 * undefined and cannot be discarded while merging it with the backedge.
 * Found via SDCC's GCC torture import gcc-torture-execute-961004-1.c.
 */
#include <stdio.h>

static int gate;

int main(void)
{
    int i;
    int saved;

    gate = 0;
    for (i = 0; i < 2; ++i) {
        if (gate) {
            if (saved != 2) {
                printf("tloopdef failed: got %d expected 2\n", saved);
                return 1;
            }
        } else {
            saved = 2;
            ++gate;
        }
    }

    printf("tloopdef completed with great success\n");
    return 0;
}
