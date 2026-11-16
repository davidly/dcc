/* Block locals in a switch, a loop or a brace block that declares nothing
 * itself.  Support checking once numbered those blocks differently from code
 * generation, so `c` in one case was taken for another case's `c`: an
 * unsigned char that should wrap to 1 kept the value 257. */
#include <stdio.h>

int fails;

void check(long got, long want, int line)
{
    if (got != want) {
        printf("line %d: got %ld, expected %ld\n", line, got, want);
        ++fails;
    }
}

#define CHECK(got, want) check((long)(got), (long)(want), __LINE__)

int cases(int n)
{
    int r = 0;
    switch (n) {
    case 1: { unsigned char c = 255; c += 2; r = c; break; }
    case 2: { signed char c = 127; c += 1; r = c; break; }
    default: { int c = -1; r = c >> 1; }
    }
    return r;
}

long wide_cases(int n)
{
    long r = 0;
    switch (n) {
    case 1: { int c = -1; r = c; r <<= 4; break; }
    case 2: { long c = 70000L; r = c / 7; break; }
    default: { unsigned char c = 200; c *= 2; r = c; }
    }
    return r;
}

int nested_switch(int n, int m)
{
    int r = 0;
    switch (n) {
    case 1:
        switch (m) {
        case 1: { unsigned char c = 250; c += 10; r = c; break; }
        default: { int c = 1000; r = c; }
        }
        break;
    default: { int c = -3; r = c; }
    }
    return r;
}

int loop_cases(int n)
{
    int r = 0;
    int i;
    for (i = 0; i < n; ++i) {
        switch (i & 3) {
        case 0: { unsigned char c = 254; c += 3; r += c; break; }
        case 1: { signed char c = -128; c -= 1; r += c; break; }
        default: { int c = 2; r += c; }
        }
    }
    return r;
}

int plain_block(int n)
{
    int r = 0;
    {
        if (n) { long c = 100000L; r = (int)(c / 1000); }
        { unsigned char c = 255; c += n; r += c; }
        { int c = -n; r += c; }
    }
    return r;
}

int main()
{
    CHECK(cases(1), 1);
    CHECK(cases(2), -128);
    CHECK(cases(3), -1);
    CHECK(wide_cases(1), -16L);
    CHECK(wide_cases(2), 10000L);
    CHECK(wide_cases(3), 144L);
    CHECK(nested_switch(1, 1), 4);
    CHECK(nested_switch(1, 2), 1000);
    CHECK(nested_switch(2, 1), -3);
    CHECK(loop_cases(6), 1 + 127 + 2 + 2 + 1 + 127);
    CHECK(plain_block(2), 100 + 1 - 2);
    if (fails == 0)
        printf("tswblk passed\n");
    return fails;
}
