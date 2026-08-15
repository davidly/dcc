/* tsjdeep.c - setjmp/longjmp regression test through real call depth.
 *
 * tsetjmp.c covers the basic one-level case; this exercises the saved
 * SP/IX across several nested stack frames (each with its own locals)
 * and across repeated setjmp/longjmp cycles, which is what actually
 * stresses the runtime's frame-restoration logic.
 */

#include <stdio.h>
#include <setjmp.h>

static jmp_buf env;
static int fails;

/* burn stack depth with locals between setjmp and the eventual longjmp,
 * so the saved SP/IX must survive several real call frames, not just one */
static long deep(int level, int a, int b, int c)
{
    long local[4];
    int i;

    local[0] = a + level;
    local[1] = b + level;
    local[2] = c + level;
    local[3] = a + b + c + level;

    if (level == 0) {
        longjmp(env, 42);
        return -1; /* unreachable */
    }

    for (i = 0; i < 4; i++)
        local[i] += deep(level - 1, a + 1, b + 2, c + 3);

    return local[0] + local[1] + local[2] + local[3];
}

static void check(const char *name, int cond)
{
    if (!cond) {
        printf("FAIL %s\n", name);
        fails++;
    }
}

int main(void)
{
    volatile int cycle;
    int r;

    fails = 0;

    /* repeated setjmp/longjmp cycles through real recursion depth */
    for (cycle = 0; cycle < 5; cycle++) {
        int marker = 1000 + cycle;

        r = setjmp(env);
        if (r == 0) {
            check("marker before jump", marker == 1000 + cycle);
            deep(8, 1, 2, 3);
            check("unreachable after deep", 0);
        } else {
            check("longjmp value", r == 42);
            check("marker after jump", marker == 1000 + cycle);
        }
    }

    if (fails) {
        printf("tsjdeep FAILED: %d\n", fails);
        return 1;
    }
    printf("tsjdeep: all tests passed\n");
    return 0;
}
