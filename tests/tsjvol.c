/* A volatile local keeps the value stored before a call that longjmps back.
 * dccpeep's dead frame-store pass deleted n = 11 because n = -99 follows the
 * call, though the call returns a second time through setjmp, whose code
 * reads n. */
#include <stdio.h>
#include <setjmp.h>

static jmp_buf jb;

static void jump(int v)
{
    longjmp(jb, v);
}

static int after_store(void)
{
    volatile int n;
    int r;

    n = 0;
    r = setjmp(jb);
    if (r == 0) {
        n = 11;
        jump(3);
        n = -99;
    }
    return r * 100 + n;
}

int main(void)
{
    int got = after_store();

    if (got != 311)
        printf("tsjvol failed: %d, expected 311\n", got);
    else
        printf("tsjvol passed\n");
    return 0;
}
