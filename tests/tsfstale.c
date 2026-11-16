/* `x = t + k` where t is x's earlier value and x has changed since.  The
 * in-place update matchers took the load of x as still being in memory and
 * added k to x itself: t = x; x--; x = t + 127 gave x - 1 + 127. */
#include <stdio.h>

int fails;
unsigned short gw;

static void check(unsigned got, unsigned want, int line)
{
    if (got != want) {
        printf("line %d: got %u, expected %u\n", line, got, want);
        ++fails;
    }
}

unsigned local_after_byte_store(void)
{
    unsigned short x = 0x100;
    ((unsigned char *)&x)[1] = 0;
    {
        unsigned t = x;
        x--;
        x = t + 127u;
    }
    return x;
}

unsigned local_increment(void)
{
    unsigned short x = 0x100;
    unsigned short *p = &x;
    unsigned t;
    *p = 5;
    t = x;
    x += 3;
    x = t + 1;
    return x;
}

void global_adjust(void)
{
    unsigned t = gw;
    gw -= 9;
    gw = t + 2;
}

int main()
{
    check(local_after_byte_store(), 127, __LINE__);
    check(local_increment(), 6, __LINE__);
    gw = 40;
    global_adjust();
    check(gw, 42, __LINE__);
    if (fails == 0)
        printf("tsfstale passed\n");
    return fails;
}
