/* A long local whose value is a known constant, stored to a short through an
 * index: the store reads the constant's low word, and the rematerialized
 * constant has no stack slot, so the store read an unallocated slot. */
#include <stdio.h>

int fails;
short r[3];

static void check(long got, long want, int line)
{
    if (got != want) {
        printf("line %d: got %ld, expected %ld\n", line, got, want);
        ++fails;
    }
}

void reassigned(short a, int i)
{
    r[i] = a;
    {
        long t = (unsigned char)156L;
        t = (short)7L;
        t = (unsigned char)128L;
        r[i] = t + (short)6239L;
    }
}

void twice(int i) { { long t = 156; t = 128; r[i] = t + 6239; } }
void once(int i) { { long t = 128; r[i] = t + 6239; } }
void outer(int i) { long t = 128; r[i] = t + 6239; }

int main()
{
    reassigned(-1, 0);
    check(r[0], 6367, __LINE__);
    twice(1);
    check(r[1], 6367, __LINE__);
    once(2);
    check(r[2], 6367, __LINE__);
    r[1] = 0;
    outer(1);
    check(r[1], 6367, __LINE__);
    if (fails == 0)
        printf("twconst passed\n");
    return fails;
}
