/* An inner block's `t` shadowing an enclosing `t` of the same type.  Block
 * locals were matched to MIR objects by name and type, so after an earlier
 * block, a loop or a switch had shifted the block numbering, the inner `t`
 * could be given the enclosing `t`'s object: its initializer or assignment
 * overwrote the outer value, or a read of it returned the outer one once a
 * later block's `t` had taken its object over. */
#include <stdio.h>

int fails;

static long pass(long v) { return v; }

static void check(long got, long want, int line)
{
    if (got != want) {
        printf("line %d: got %ld, expected %ld\n", line, got, want);
        ++fails;
    }
}

unsigned char s;

unsigned after_sibling(unsigned a1, unsigned long a3)
{
    unsigned char *p = &s;
    {
        unsigned t = 1;
    }
    {
        unsigned t = *p;
        {
            unsigned t = ~(a1 ? a3 : s);
        }
        return t;
    }
}

long in_loop(int k0, signed char a3)
{
    long l4 = 14;
    {
        int j = 1;
        do {
        } while (--j > 0);
        for (j = 0; j < 3; ++j) {
            {
                unsigned char t = 1;
                {
                    unsigned char t = k0;
                    t = pass(a3);
                }
                {
                }
                l4 = (t | t);
            }
        }
        {
        }
    }
    return l4;
}

unsigned long g;

long after_switch(long a2, short *p)
{
    switch (*p & 3) {
        {
            {
            }
        }
        {
        }
        {
        }
    }
    {
        unsigned long t = a2;
        {
            unsigned long t = (0 ^ (short)-28) ^ !*p;
            a2 = t;
        }
    }
    {
        unsigned long t = 5;
        {
            unsigned long t = 6;
            g = t;
        }
        g += t;
    }
    return a2;
}

int main()
{
    short zero = 0;
    s = 9;
    check(after_sibling(3, 55), 9, __LINE__);
    check(in_loop(256, -100), 1, __LINE__);
    check(after_switch(-25, &zero), -27, __LINE__);
    check((long)g, 11, __LINE__);
    if (fails == 0)
        printf("tblkshd passed\n");
    return fails;
}
