/* tstrtri.c - aggregate-call and pointer-store MIR regressions */

#include <stdio.h>

typedef struct { char value; } Tiny;

static Tiny old_copy(x)
Tiny x;
{
    Tiny result;
    result.value = x.value;
    return result;
}

struct Holder { int *pointer; } source, destination;
static int marker;

static void store_pointer(int **slot)
{
    *slot = &marker;
}

int main(void)
{
    Tiny input, output;

    printf("tstrtri start\n");
    input.value = 100;
    output = old_copy(input);
    if (output.value != 100) {
        printf("FAIL old-style struct argument\n");
        return 1;
    }

    source.pointer = 0;
    store_pointer(&source.pointer);
    destination = source;
    if (destination.pointer != &marker) {
        printf("FAIL pointer store and struct assignment\n");
        return 1;
    }

    printf("tstrtri completed with great success\n");
    return 0;
}
