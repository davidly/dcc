/*
 * tgcdphi.c - loop PHI fallthrough regression.
 * Generated archive case: batch4/c8908.
 */
#include <stdio.h>

static int gcd(int first, int second)
{
    while (second) {
        int next = first % second;
        first = second;
        second = next;
    }
    return first < 0 ? -first : first;
}

static int lcm(int first, int second)
{
    return first / gcd(first, second) * second;
}

int main(void)
{
    int first = lcm(12, 18);
    int second = lcm(14, 20);
    int ok = first == 36 && second == 140;

    printf("tgcdphi lcm=%d,%d\n", first, second);
    if (ok)
        printf("tgcdphi passed with great success\n");
    return !ok;
}
