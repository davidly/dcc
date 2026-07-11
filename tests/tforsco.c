/*
 * tforsco.c - C99 for-init declaration scoping.
 *
 * dcc accepts  for (int i = 0; ...)  syntax, but the loop variable used to
 * share one flat function scope: a for-init `int i` whose name already named
 * a local/param aliased that outer variable instead of shadowing it.  These
 * tests pin the corrected block-scoped behaviour:
 *   - a for-init variable that shadows an outer one leaves the outer value
 *     intact after the loop, and
 *   - sibling and nested loops (including a nested shadow) each get their own
 *     loop variable.
 */
#include <stdio.h>

static int fails;
static void chk(long got, long want, char *name)
{
    if (got != want) { printf("FAIL %s got=%ld want=%ld\n", name, got, want); fails++; }
}

static int param_shadow(int p)
{
    int got = 0;
    for (int p = 0; p < 3; p++)
        got += p;
    return got * 100 + p;
}

static int plus_one(int value)
{
    return value + 1;
}

int main(void)
{
    int sum = 0;
    int i;
    int m;

    /* basic for-init */
    for (int i = 0; i < 5; i++)
        sum += i;
    chk(sum, 10L, "basic");

    /* shadowing: the loop i must not touch the outer i */
    i = 99;
    for (int i = 0; i < 3; i++)
        sum += i;                       /* +0+1+2 -> 13 */
    chk(i, 99L, "shadow outer untouched");
    chk(sum, 13L, "shadow sum");

    /* sibling loops each re-declare i */
    for (int i = 0; i < 4; i++) sum += i;   /* +6 -> 19 */
    for (int i = 0; i < 4; i++) sum += i;   /* +6 -> 25 */
    chk(sum, 25L, "siblings");

    /* nested loops, distinct names */
    {
        int total = 0;
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 2; b++)
                total++;
        chk(total, 6L, "nested distinct");
    }

    /* nested shadow: inner for-j shadows an outer j */
    {
        int j = 7;
        long acc = 0;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                acc += j;               /* 3 * (0+1+2) = 9 */
        chk(j, 7L, "nested shadow outer j");
        chk(acc, 9L, "nested shadow acc");
    }

    /* for-init shadow with a different type than the outer variable */
    {
        int k = 1234;
        long ksum = 0;
        for (long k = 100000L; k < 100003L; k++)
            ksum += k;                  /* 100000+100001+100002 = 300003 */
        chk(k, 1234L, "type-diff shadow outer");
        chk(ksum, 300003L, "type-diff shadow sum");
    }

    /* non-shadowing for-init still works (no outer m) */
    {
        long msum = 0;
        for (int m = 0; m < 6; m++)
            msum += m;                  /* 15 */
        chk(msum, 15L, "non-shadow");
    }

    /* C99 loop scope also hides a non-shadowing init declaration after loop. */
    m = 77;
    for (int m = 0; m < 2; m++)
        sum += m;                       /* +1 -> 26 */
    chk(m, 77L, "non-shadow hidden after");
    chk(sum, 26L, "non-shadow hidden sum");

    /* init declaration stays in scope for condition, body, and increment. */
    {
        int c = 500;
        int seen = 0;
        for (int c = 0; c < 5; c++) {
            if (c == 1) continue;
            seen += c;
            if (c == 3) break;
        }
        chk(c, 500L, "continue break outer");
        chk(seen, 5L, "continue break seen");
    }

    /* single-statement and empty-body loops must keep the init name scoped. */
    {
        int s = 20;
        int one = 0;
        int empty = 0;
        for (int s = 0; s < 3; s++) one += s;
        for (int e = 0; e < 4; empty += e, e++) ;
        chk(s, 20L, "single statement outer");
        chk(one, 3L, "single statement sum");
        chk(empty, 6L, "empty body increment");
    }

    /* multiple declarators: each declared name enters scope after its own
     * declarator, so the initializer for b sees the new a. */
    {
        int a = 100;
        int b = 200;
        int multi = 0;
        for (int a = 1, b = a + 2; a < 4; a++, b += 10)
            multi += a + b;             /* 1+3 + 2+13 + 3+23 = 45 */
        chk(a, 100L, "multi outer a");
        chk(b, 200L, "multi outer b");
        chk(multi, 45L, "multi sum");
    }

    /* pointer declarator in a for-init should scope like any other declarator. */
    {
        int vals[3];
        int *p;
        int psum = 0;
        vals[0] = 2;
        vals[1] = 4;
        vals[2] = 6;
        p = vals;
        for (int *p = vals; p < vals + 3; p++)
            psum += *p;
        chk(*p, 2L, "pointer outer");
        chk(psum, 12L, "pointer sum");
    }

    /* Arrays and function pointers are automatic objects too.  Keep them
     * distinct from direct function declarations, which C99/C11 do not allow
     * in the declaration part of a for statement. */
    {
        int asum = 0;
        int fvalue = 7;
        for (int k = 0, vals[2] = { 3, 4 }; k < 2; k++)
            asum += vals[k];
        for (int (*fn)(int) = plus_one; fvalue < 9; fvalue = fn(fvalue))
            ;
        chk(asum, 7L, "array object declaration");
        chk(fvalue, 9L, "function pointer object declaration");
    }

    /* auto and register are the only storage classes C99/C11 permit in a
     * for-init declaration.  dcc has no Z80 register allocation, so the hint
     * is a no-op and both behave as ordinary block-scoped auto locals. */
    {
        int rsum = 0;
        int asum = 0;
        for (register int r = 0; r < 3; r++)
            rsum += r;
        for (auto int a = 0; a < 3; a++)
            asum += a;
        chk(rsum, 3L, "register for-init");
        chk(asum, 3L, "auto for-init");
    }

    /* a for-init declaration can shadow a parameter. */
    chk(param_shadow(7), 307L, "param shadow");

    /* const for-init names still need storage when their address is taken. */
    {
        int q = 10;
        int qgot = 0;
        for (const int q = 5; qgot == 0; qgot++) {
            int *qp = &q;
            qgot += *qp;
        }
        chk(q, 10L, "const address outer");
        chk(qgot, 6L, "const address value");
    }

    if (fails == 0) printf("tforsco passed with great success\n");
    else printf("tforsco FAILED: %d\n", fails);
    return fails != 0;
}
