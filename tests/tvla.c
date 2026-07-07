/*
 * tvla.c - C99 variable-length array support (the subset dcc implements:
 * a local array whose only variable dimension is the outermost, with constant
 * inner dimensions).  Exercises 1-D int/char VLAs, a 2-D VLA with a constant
 * inner dimension, VLA decay to a pointer argument, and a computed size.
 * Output is a deterministic PASS/FAIL summary.
 */
#include <stdio.h>
#include <setjmp.h>

static int checks = 0;
static int failures = 0;

static void check_int(const char *name, int got, int want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s got %d want %d\n", name, got, want);
    }
}

/* The VLA decays to a plain pointer here, exactly like a fixed array. */
static int sum_ints(int *p, int n)
{
    int i;
    int s = 0;
    for (i = 0; i < n; i++)
        s += p[i];
    return s;
}

static int vla_1d(int n)
{
    int a[n];
    int i;
    for (i = 0; i < n; i++)
        a[i] = i * i;
    return sum_ints(a, n);
}

static int vla_char(int n)
{
    char buf[n];
    int i;
    for (i = 0; i < n - 1; i++)
        buf[i] = (char)('a' + i);
    buf[n - 1] = 0;
    return (int)buf[0] + (int)buf[n - 2];
}

static int vla_2d(int rows)
{
    int a[rows][3];
    int i, j, s;
    for (i = 0; i < rows; i++)
        for (j = 0; j < 3; j++)
            a[i][j] = i * 10 + j;
    s = 0;
    for (i = 0; i < rows; i++)
        for (j = 0; j < 3; j++)
            s += a[i][j];
    return s;
}

static int vla_computed(int n)
{
    int a[n * 2 + 1];
    int i;
    int count = n * 2 + 1;
    for (i = 0; i < count; i++)
        a[i] = 1;
    return sum_ints(a, count);
}

static int vla_parenthesized_bound(int n)
{
    int a[(n + 1)];
    a[n] = 17;
    return a[n];
}

static int vla_leading_const_bound(int n)
{
    int a[1 + n];
    a[n] = 19;
    return a[n];
}

/* Bound expression containing an array subscript: the dimension parser must
 * skip to the dimension's own ']' without stopping on the inner subscript. */
static int vla_subscript_bound(int n)
{
    int len[1];
    len[0] = n;
    {
        int a[len[0] + 2];
        int i, m = len[0] + 2, s = 0;
        for (i = 0; i < m; i++) a[i] = i;
        for (i = 0; i < m; i++) s += a[i];
        return s;                       /* 0+1+...+(n+1) */
    }
}

/* Bound expression that is a function call. */
static int idn(int x) { return x; }
static int vla_call_bound(int n)
{
    int a[idn(n)];
    int i, s = 0;
    for (i = 0; i < n; i++) a[i] = 2;
    for (i = 0; i < n; i++) s += a[i];
    return s;                           /* 2*n */
}

/* A second VLA whose bound subscripts the FIRST (also a VLA).  This is the
 * exact shape the dimension parser once mis-handled: the inner subscript's ']'
 * must not terminate the outer dimension. */
static int vla_dependent_bound(int n)
{
    int a[n];
    int i, s = 0;
    for (i = 0; i < n; i++) a[i] = i + 1;
    {
        int b[a[n - 1] + 2];            /* size = n + 2, from a VLA subscript */
        int m = a[n - 1] + 2;
        for (i = 0; i < m; i++) b[i] = i;
        for (i = 0; i < m; i++) s += b[i];   /* 0+1+...+(n+1) */
    }
    for (i = 0; i < n; i++) s += a[i];       /* + (1+2+...+n) */
    return s;
}

/* Bounds using sizeof of an identifier are constant expressions, not VLAs. */
static int fixed_sizeof_bounds(int n)
{
    int a[sizeof n];
    int b[sizeof n + 1];
    int acount;
    int bcount;

    acount = (int)(sizeof a / sizeof a[0]);
    bcount = (int)(sizeof b / sizeof b[0]);
    return (acount == (int)sizeof n) &&
           (bcount == (int)sizeof n + 1);
}

/* A cast of a constant bound is a constant expression, not a VLA: the cast's
 * type-name (e.g. size_t) must not be mistaken for a runtime dimension. */
static int fixed_cast_bounds(void)
{
    int a[(size_t)8];
    int b[(unsigned)4 + 1];
    int acount = (int)(sizeof a / sizeof a[0]);
    int bcount = (int)(sizeof b / sizeof b[0]);
    return (acount == 8) && (bcount == 5);
}

/* Three-dimensional VLA: variable outer, constant inner dims. */
static int vla_3d(int n)
{
    int a[n][2][3];
    int i, j, k, s = 0;
    for (i = 0; i < n; i++)
        for (j = 0; j < 2; j++)
            for (k = 0; k < 3; k++)
                a[i][j][k] = i + j + k;
    for (i = 0; i < n; i++)
        for (j = 0; j < 2; j++)
            for (k = 0; k < 3; k++)
                s += a[i][j][k];
    return s;
}

/* A VLA declared inside a loop body must be reclaimed each iteration (C99
 * block scope). Running many iterations would overflow the CP/M stack if it
 * leaked; the accumulation is kept within 16-bit range so the result matches
 * on both a 16-bit-int target and a 32-bit host. */
static int vla_in_loop(int iters, int n)
{
    int i, j;
    int last = 0;
    for (i = 0; i < iters; i++) {
        int a[n];
        for (j = 0; j < n; j++)
            a[j] = (i + j) & 15;
        last = a[n - 1];
        if ((i & 1) == 0)
            continue;               /* continue must reclaim too */
        last += a[0];
    }
    return last;
}

/* Nested block scopes, each with its own VLA, inside a loop. */
static int vla_nested(int m)
{
    int total = 0;
    int k;
    for (k = 0; k < m; k++) {
        char b[k + 2];
        int t;
        for (t = 0; t < k + 1; t++)
            b[t] = (char)t;
        {
            int inner[k + 1];
            int u;
            for (u = 0; u <= k; u++)
                inner[u] = b[u];
            total += inner[k];
        }
    }
    return total;
}

static int vla_goto_out(int iters, int n)
{
    int i;
    int value = 0;
    for (i = 0; i < iters; i++) {
        {
            int a[n];
            a[0] = i & 15;
            value = a[0];
            goto out;
        }
out:
        ;
    }
    return value;
}

/* Bound is the for-init loop variable; size changes each iteration. */
static int vla_forinit_dep(int n)
{
    int i, s = 0;
    for (i = 1; i <= n; i++) {
        int a[i];
        int j;
        for (j = 0; j < i; j++) a[j] = 1;
        for (j = 0; j < i; j++) s += a[j];
    }
    return s;                           /* 1+2+...+n */
}

/* A fixed local declared after the VLA still gets a correct frame slot. */
static int vla_fixed_after(int n)
{
    int a[n];
    int tail = 77;
    int i, s = 0;
    for (i = 0; i < n; i++) a[i] = i;
    for (i = 0; i < n; i++) s += a[i];
    return s + tail;
}

/* A large fixed array in the same scope pushes the VLA slots past the
 * signed-byte IX offset range, exercising the ld de,offset save/restore path. */
static int vla_big_frame(int n)
{
    int big[80];
    int a[n];
    int i, s = 0;
    for (i = 0; i < 80; i++) big[i] = 1;
    for (i = 0; i < n; i++) a[i] = 2;
    for (i = 0; i < 80; i++) s += big[i];
    for (i = 0; i < n; i++) s += a[i];
    return s;                           /* 80 + 2n */
}

/* Conditional sibling VLAs in if/else at the same block depth. */
static int vla_cond_sibling(int n, int c)
{
    int s = 0, i;
    if (c) {
        int a[n];
        for (i = 0; i < n; i++) a[i] = 3;
        for (i = 0; i < n; i++) s += a[i];
    } else {
        int b[n + 1];
        for (i = 0; i < n + 1; i++) b[i] = 5;
        for (i = 0; i < n + 1; i++) s += b[i];
    }
    return s;
}

/* A side-effecting bound is evaluated exactly once. */
static int vla_side_bound(int n)
{
    int calls = n;
    int a[calls++];
    int i, s = 0;
    for (i = 0; i < n; i++) a[i] = 4;
    for (i = 0; i < n; i++) s += a[i];
    return s + calls;                   /* 4n + (n+1) */
}

/* VLAs in distinct switch case blocks, including fall-through. */
static int vla_switch(int n, int x)
{
    int s = 0, i;
    switch (x) {
    case 1: {
        int a[n];
        for (i = 0; i < n; i++) a[i] = 1;
        for (i = 0; i < n; i++) s += a[i];
    }   /* fall through */
    case 2: {
        int b[n + 1];
        for (i = 0; i < n + 1; i++) b[i] = 10;
        for (i = 0; i < n + 1; i++) s += b[i];
        break;
    }
    default:
        s = -1;
    }
    return s;
}

/* return from within a nested VLA block; the epilogue restores SP. */
static int vla_return_nested(int n)
{
    {
        int a[n];
        int i, s = 0;
        for (i = 0; i < n; i++) a[i] = 2;
        for (i = 0; i < n; i++) s += a[i];
        return s;                       /* 2n */
    }
}

/* setjmp/longjmp across a VLA: longjmp unwinds SP past the allocation. */
static jmp_buf vla_jb;
static int vla_longjmp(int n)
{
    int hit = 0;
    if (setjmp(vla_jb) != 0)
        return 999;
    {
        int a[n];
        int i;
        for (i = 0; i < n; i++) a[i] = i;
        hit = a[n - 1];
        longjmp(vla_jb, 1);
    }
    return hit;
}

/* VLA of pointers (element size is a target pointer, not the base type). */
static char vla_pool[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
static int vla_ptr_elem(int n)
{
    char *a[n];
    int i, s = 0;
    for (i = 0; i < n; i++) a[i] = &vla_pool[i];
    for (i = 0; i < n; i++) s += (int)*a[i];
    return s;                           /* 0+1+...+(n-1) */
}

/* Pointer arithmetic and difference within a VLA. */
static int vla_ptr_diff(int n)
{
    int a[n];
    int *p = a;
    int *q = a + (n - 1);
    a[0] = 5;
    a[n - 1] = 9;
    return (int)(q - p) + *q - *p;      /* (n-1) + 9 - 5 */
}

/* A long-typed bound expression cast to int. */
static int vla_long_bound(int n)
{
    long m = (long)n + 1;
    int a[(int)m];
    int i, s = 0;
    for (i = 0; i < (int)m; i++) a[i] = 1;
    for (i = 0; i < (int)m; i++) s += a[i];
    return s;                           /* n+1 */
}

/* A 2-D VLA passed to a callee that indexes it as g[][3]. */
static int vla_sum2d(int rows, int g[][3])
{
    int i, j, s = 0;
    for (i = 0; i < rows; i++)
        for (j = 0; j < 3; j++)
            s += g[i][j];
    return s;
}
static int vla_pass2d(int rows)
{
    int grid[rows][3];
    int i, j;
    for (i = 0; i < rows; i++)
        for (j = 0; j < 3; j++)
            grid[i][j] = i + j;
    return vla_sum2d(rows, grid);
}

/* Forward goto whose target label is in the SAME VLA scope (no SP change). */
static int vla_fwd_same(int n)
{
    int a[n];
    int i, s = 0;
    for (i = 0; i < n; i++)
        a[i] = i;
    if (a[0] == 0)
        goto here;
    s += 999;
here:
    for (i = 0; i < n; i++)
        s += a[i];
    return s;                     /* 0+1+2+3+4 = 10 for n=5 */
}

/* Forward goto that exits an inner VLA scope but stays inside an outer VLA
 * scope; the outer VLA must remain valid after the jump. */
static int vla_fwd_exit_inner(int n)
{
    int a[n];
    int i, s = 0;
    for (i = 0; i < n; i++)
        a[i] = 1;
    {
        int b[n * 2];
        for (i = 0; i < n * 2; i++)
            b[i] = 2;
        if (b[0] == 2)
            goto done;            /* reclaim b, keep a */
        s += 555;
    }
done:
    for (i = 0; i < n; i++)
        s += a[i];
    return s;                     /* n for n>0 */
}

/* Forward goto that exits every VLA scope, landing on a non-VLA label. */
static int vla_fwd_exit_all(int n)
{
    int s = 0;
    {
        int a[n];
        int i;
        for (i = 0; i < n; i++)
            a[i] = 3;
        if (a[0] == 3)
            goto out;
        s += 1;
    }
out:
    return s + 7;                 /* 7 */
}

/* Forward goto inside a loop, jumping over work but staying in the VLA scope;
 * the loop then reclaims and re-allocates the VLA each iteration. */
static int vla_fwd_in_loop(int n)
{
    int i, s = 0;
    for (i = 0; i < n; i++) {
        int a[n];
        int j;
        for (j = 0; j < n; j++)
            a[j] = j;
        if (a[0] == 0)
            goto skip;
        s += 100;
    skip:
        s += a[i];
    }
    return s;                     /* 0+1+2+3+4 = 10 for n=5 */
}

/* Backward goto that exits an inner VLA scope while staying inside an outer VLA
 * scope; the inner VLA must be reclaimed on every backward jump so SP does not
 * leak, and the outer VLA must stay valid across the jumps. */
static int vla_back_exit_inner(int iters, int n)
{
    int a[n];
    int i = 0, s = 0;
    a[0] = 7;
again:
    {
        int b[n];
        b[0] = i;
        s = a[0] + (b[0] & 15);   /* a survives the backward jump */
        i++;
        if (i < iters)
            goto again;           /* reclaim b, back into a's scope */
    }
    return s;                     /* i=2999 -> 7 + (2999&15) = 14 */
}

int main(void)
{
    printf("tvla start\n");

    /* 1-D: 0+1+4+9+16 = 30 */
    check_int("vla_1d(5)", vla_1d(5), 30);
    /* 0+1+4+...+81 = 285 */
    check_int("vla_1d(10)", vla_1d(10), 285);

    /* 'a' (97) + buf[4]='e' (101) = 198 */
    check_int("vla_char(6)", vla_char(6), 198);

    /* rows=2: (0+1+2)+(10+11+12) = 36 */
    check_int("vla_2d(2)", vla_2d(2), 36);
    /* rows=4: 3+33+63+93 = 192 */
    check_int("vla_2d(4)", vla_2d(4), 192);

    /* size = 3*2+1 = 7 ones */
    check_int("vla_computed(3)", vla_computed(3), 7);

    check_int("vla_paren", vla_parenthesized_bound(4), 17);
    check_int("vla_leadconst", vla_leading_const_bound(4), 19);

    /* Bound with a subscript: sum 0..(n+1) for n=5 -> 21 */
    check_int("vla_subscript_bound", vla_subscript_bound(5), 21);
    /* Bound is a call: 2*n for n=6 -> 12 */
    check_int("vla_call_bound", vla_call_bound(6), 12);
    /* Second VLA sized from a subscript of the first VLA:
     * sum 0..(n+1) plus 1..n for n=5 -> 21 + 15 = 36 */
    check_int("vla_dependent_bound", vla_dependent_bound(5), 36);
    /* sizeof n in an array bound is a fixed-size array, not a VLA. */
    check_int("fixed_sizeof_bounds", fixed_sizeof_bounds(123), 1);
    /* A cast of a constant bound is a fixed-size array, not a VLA. */
    check_int("fixed_cast_bounds", fixed_cast_bounds(), 1);
    /* 3-D VLA (variable outer, constant inner) for n=3 -> 45 */
    check_int("vla_3d", vla_3d(3), 45);

    /* 3000 iterations must not overflow the stack; last iter (i=2999, odd):
     * a[7]=(3006)&15=14, a[0]=(2999)&15=7 -> 21 */
    check_int("vla_in_loop", vla_in_loop(3000, 8), 21);

    /* sum of inner[k] = b[k] = k, for k=0..9 -> 45 */
    check_int("vla_nested", vla_nested(10), 45);

    /* Forward goto out of a VLA scope must reclaim the allocation. */
    check_int("vla_goto_out", vla_goto_out(3000, 8), 7);

    /* --- edge cases: distinct frame / control-flow / decay paths --- */
    check_int("vla_forinit_dep", vla_forinit_dep(5), 15);
    check_int("vla_fixed_after", vla_fixed_after(6), 92);
    check_int("vla_big_frame", vla_big_frame(4), 88);
    check_int("vla_cond_true", vla_cond_sibling(4, 1), 12);
    check_int("vla_cond_false", vla_cond_sibling(4, 0), 25);
    check_int("vla_side_bound", vla_side_bound(5), 26);
    check_int("vla_switch_fall", vla_switch(3, 1), 43);
    check_int("vla_switch_one", vla_switch(3, 2), 40);
    check_int("vla_switch_def", vla_switch(3, 9), -1);
    check_int("vla_return_nested", vla_return_nested(6), 12);
    check_int("vla_longjmp", vla_longjmp(5), 999);
    check_int("vla_ptr_elem", vla_ptr_elem(6), 15);
    check_int("vla_ptr_diff", vla_ptr_diff(4), 7);
    check_int("vla_long_bound", vla_long_bound(5), 6);
    check_int("vla_pass2d", vla_pass2d(4), 30);

    check_int("vla_fwd_same", vla_fwd_same(5), 10);
    check_int("vla_fwd_exit_inner", vla_fwd_exit_inner(4), 4);
    check_int("vla_fwd_exit_all", vla_fwd_exit_all(3), 7);
    check_int("vla_fwd_in_loop", vla_fwd_in_loop(5), 10);
    check_int("vla_back_exit_inner", vla_back_exit_inner(3000, 8), 14);

    printf("checks=%d failures=%d\n", checks, failures);
    if (failures != 0) {
        printf("tvla FAIL\n");
        return 1;
    }
    printf("tvla ok\n");
    return 0;
}
