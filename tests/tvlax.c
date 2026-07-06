/*
 * tvlax.c - stress test for C99 variable-length arrays, focused on stack
 * reclamation (no stack-pointer leakage) across loops, recursion, nested
 * scopes, and goto-out block exits.
 *
 * Key idea: the address of a VLA's first element is the stack pointer right
 * after the block allocates the array.  So capturing (the low bits of)
 * &a[0] and comparing it across iterations / recursion levels / repeated
 * calls is a portable, well-defined (pointer-equality only) probe of the
 * stack pointer:
 *
 *   - in a loop, a correctly reclaimed VLA is re-allocated at the SAME
 *     address every iteration; a leak makes the address drift downward;
 *   - after a function fully returns, the stack is empty again, so a second
 *     identical call must place its VLA at the SAME address as the first;
 *   - a deeper recursion frame must sit at a DIFFERENT (lower) address than
 *     its caller, and the caller's data must survive the deeper call.
 *
 * dcc treats `volatile` as a no-op (source compatibility only), so this test
 * deliberately avoids relying on it: the address-stability checks work purely
 * through defined pointer/integer comparisons and hold identically under a
 * host compiler (clang) and under dcc.  All arithmetic is kept within 16-bit
 * range so a 16-bit-int target and a 32-bit host agree.  Output is a
 * deterministic PASS/FAIL summary; the baseline is generated with clang.
 */
#include <stdio.h>

static int checks = 0;
static int failures = 0;

static void ok(const char *name, int cond)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL %s\n", name);
    }
}

/*
 * Low bits of a pointer as an unsigned integer.  On the dcc target this is the
 * full 16-bit pointer; on a 32/64-bit host it is the low 32 bits.  Either way,
 * equality and inequality of stack addresses are preserved, which is all these
 * checks rely on.
 */
static unsigned addr_of(void *p)
{
    return (unsigned)(unsigned long)p;
}

/* ---- 1. Loop: the VLA base address must not drift across iterations ---- */
static int loop_stable(int iters, int n)
{
    unsigned first = 0;
    int stable = 1;
    int i, j, guard = 0;

    for (i = 0; i < iters; i++) {
        int a[n];
        for (j = 0; j < n; j++)
            a[j] = (i + j) & 7;
        if (i == 0)
            first = addr_of(&a[0]);
        else if (addr_of(&a[0]) != first)
            stable = 0;
        guard += a[n - 1];
    }
    (void)guard;
    return stable;
}

/* ---- 2. Recursion: frames distinct, data intact, reclaimed between calls -- */
static int rec_distinct = 1;
static int rec_intact = 1;

static unsigned rec_probe(int depth, int n)
{
    int a[n];
    int j;
    unsigned base = addr_of(&a[0]);

    for (j = 0; j < n; j++)
        a[j] = (depth * 3 + j) & 255;

    if (depth > 0) {
        unsigned deeper = rec_probe(depth - 1, n);
        if (deeper == base)
            rec_distinct = 0;          /* deeper frame overlapped this one */
    }

    /* Our own VLA must be untouched by the deeper call. */
    for (j = 0; j < n; j++)
        if (a[j] != ((depth * 3 + j) & 255))
            rec_intact = 0;

    return base;
}

/* ---- 3. Nested loops: outer and inner VLA bases both stable ---- */
static int nested_stable(int outer, int inner, int n)
{
    unsigned ofirst = 0;
    unsigned ifirst = 0;
    int ostable = 1;
    int istable = 1;
    int i, k, j, guard = 0;

    for (i = 0; i < outer; i++) {
        int oa[n];
        for (j = 0; j < n; j++)
            oa[j] = (i + j) & 15;
        if (i == 0)
            ofirst = addr_of(&oa[0]);
        else if (addr_of(&oa[0]) != ofirst)
            ostable = 0;

        for (k = 0; k < inner; k++) {
            int ia[n];
            for (j = 0; j < n; j++)
                ia[j] = (k + j) & 15;
            if (i == 0 && k == 0)
                ifirst = addr_of(&ia[0]);
            else if (addr_of(&ia[0]) != ifirst)
                istable = 0;
            guard += ia[n - 1];
        }
        guard += oa[n - 1];
    }
    (void)guard;
    return ostable && istable;
}

/* ---- 4. goto out of a VLA block: base stable across iterations ---- */
static int goto_stable(int iters, int n)
{
    unsigned first = 0;
    int stable = 1;
    int i, j, guard = 0;

    for (i = 0; i < iters; i++) {
        {
            int a[n];
            for (j = 0; j < n; j++)
                a[j] = (i + j) & 15;
            if (i == 0)
                first = addr_of(&a[0]);
            else if (addr_of(&a[0]) != first)
                stable = 0;
            guard += a[0];
            goto next;
        }
    next:
        guard += 1;
    }
    (void)guard;
    return stable;
}

/* ---- 5. A value check that also depends on correct reclamation ----
 * If VLAs leaked, deep repeated allocation would eventually corrupt or run
 * out of stack; keeping a running checksum that must match a known total is a
 * second, content-based confirmation that every element was addressable. */
static int checksum_loop(int iters, int n)
{
    int i, j, sum = 0;
    for (i = 0; i < iters; i++) {
        int a[n];
        for (j = 0; j < n; j++)
            a[j] = 1;
        for (j = 0; j < n; j++)
            sum = (sum + a[j]) & 0x7fff;
    }
    return sum;                        /* iters*n mod 32768 */
}

int main(void)
{
    unsigned c1;
    unsigned c2;

    printf("tvlax start\n");

    /* 1. loop address stability (SP must not leak over 3000 iterations) */
    ok("loop base stable", loop_stable(3000, 8));

    /* 2. recursion: two identical top-level calls reclaim to the same base */
    c1 = rec_probe(20, 8);
    c2 = rec_probe(20, 8);
    ok("recursion reclaimed between calls", c1 == c2);
    ok("recursion frames distinct", rec_distinct);
    ok("recursion frames intact", rec_intact);

    /* 3. nested loops: both VLA levels stable across all iterations */
    ok("nested bases stable", nested_stable(40, 40, 6));

    /* 4. goto out of a VLA scope reclaims each iteration */
    ok("goto-out base stable", goto_stable(3000, 8));

    /* 5. content checksum: 500*4 = 2000 mod 32768 */
    ok("checksum", checksum_loop(500, 4) == 2000);

    printf("checks=%d failures=%d\n", checks, failures);
    if (failures != 0) {
        printf("tvlax FAIL\n");
        return 1;
    }
    printf("tvlax ok\n");
    return 0;
}
