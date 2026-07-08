/*
 * tvla.c - C99 variable-length array support (the subset dcc implements:
 * a local array whose only variable dimension is the outermost, with constant
 * inner dimensions).  Exercises 1-D int/char VLAs, a 2-D VLA with a constant
 * inner dimension, VLA decay to a pointer argument, and a computed size.
 * Output is a deterministic PASS/FAIL summary.
 */
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

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

static int uvp_helper(int x) { return x + 1; }

/* Regression: an UNUSED VLA (its name never referenced again) that is the
 * first VLA in its scope, followed by a normal local, must NOT let the
 * dead-local prune drop the VLA's hidden #vlasp/#vlasz slots.  The codegen
 * pass used to prune the just-allocated VLA (its `bytes` is only the 2-byte
 * pointer, and g_vla_pending was already cleared), so the following local
 * aliased the saved-SP slot; the block-exit "ld sp,(#vlasp)" then restored a
 * garbage SP.  The scan pass never pruned it, so the two passes disagreed.
 * Fall through the block (so the per-block SP restore runs) and then make a
 * call, which uses SP - a clobbered SP would corrupt the call. */
static int unused_vla_prune_fallthrough(int n)
{
    int result = 0;
    {
        int unused[n];
        int marker = 22222;
        result = marker;
    }
    result = uvp_helper(result);
    return result;
}

/* Same hazard, but with live locals bracketing the block to catch a frame
 * offset that shifted between the two passes. */
static int unused_vla_prune_locals(int n)
{
    int a = 111;
    int b = 222;
    {
        int unused[n];
        int c = 333;
        a = a + c;
    }
    b = b + a;
    return b;
}

/* Stronger form of the same regression: on the old codegen path, `marker`
 * reused the hidden #vlasp offset, so block exit restored SP to &result+2.
 * The following call's argument push then overwrote result itself.  Keep the
 * address trick DCC-only so the host clang baseline stays well-defined. */
static int unused_vla_prune_sp_alias(int n)
{
#ifdef _DCC_
    int result = 1234;
    {
        int unused[n];
        int marker = (int)&result + 2;
        (void)marker;
    }
    uvp_helper(0);
    return result;
#else
    (void)n;
    uvp_helper(0);
    return 1234;
#endif
}

/* Same hidden-slot alias bug, but with the unused VLA and following local in
 * one declaration statement.  This specifically covers the declaration loop's
 * comma path after pruning the first declarator. */
static int unused_vla_prune_same_decl(int n)
{
#ifdef _DCC_
    int result = 4321;
    {
        int unused[n], marker = (int)&result + 2;
        (void)marker;
    }
    uvp_helper(0);
    return result;
#else
    (void)n;
    uvp_helper(0);
    return 4321;
#endif
}

/* ---- sizeof on a VLA (C99 6.5.3.4: the operand is a run-time value) ----
 *
 * dcc stores each VLA's run-time byte size in a hidden frame slot when the VLA
 * is allocated; `sizeof vla` loads that slot.  All expectations below are
 * written in terms of sizeof(int)/element counts so they are identical on a
 * 16-bit-int target (dcc) and a 32-bit-int host (the clang baseline). */

/* Whole 1-D VLA: byte size == n elements. */
static int vla_sizeof_1d(int n)
{
    int a[n];
    return (int)sizeof a;               /* n * sizeof(int) */
}

/* Whole 2-D VLA with a constant inner dim: n rows * 3 * sizeof(int). */
static int vla_sizeof_2d(int rows)
{
    int a[rows][3];
    return (int)sizeof a;               /* rows * 3 * sizeof(int) */
}

/* A constant-size subobject stays a compile-time constant. */
static int vla_sizeof_element(int n)
{
    int a[n];
    return (int)sizeof a[0];            /* sizeof(int) */
}

/* The pervasive element-count idiom must yield n regardless of int width. */
static int vla_sizeof_count(int n)
{
    int a[n];
    return (int)(sizeof a / sizeof a[0]);   /* n */
}

/* 2-D VLA row count and row size via sizeof. */
static int vla_sizeof_2d_rows(int rows)
{
    int a[rows][3];
    return (int)(sizeof a / sizeof a[0]);   /* rows */
}

static int vla_sizeof_2d_row(int rows)
{
    int a[rows][3];
    return (int)sizeof a[0];            /* 3 * sizeof(int) */
}

/* char VLA: element size is 1 on every target, so byte size == n. */
static int vla_sizeof_char(int n)
{
    char b[n];
    return (int)sizeof b;               /* n */
}

static int vla_sizeof_c99_type_bytes(int n)
{
    _Bool b[n];
    bool ba[n];
    signed char sc[n];
    unsigned char uc[n];
    short sh[n];
    unsigned short ush[n];
    unsigned int ui[n];
    unsigned long ul[n];
    float f[n];
    int8_t i8[n];
    uint8_t u8[n];
    int16_t i16[n];
    uint16_t u16[n];
    int32_t i32[n];
    uint32_t u32[n];
        int_least8_t li8[n];
        uint_least8_t lu8[n];
        int_least16_t li16[n];
        uint_least16_t lu16[n];
        int_least32_t li32[n];
        uint_least32_t lu32[n];
        int_fast8_t fi8[n];
        uint_fast8_t fu8[n];
        int_fast16_t fi16[n];
        uint_fast16_t fu16[n];
        int_fast32_t fi32[n];
        uint_fast32_t fu32[n];
        intmax_t imax[n];
        uintmax_t umax[n];
    return (int)sizeof b + (int)sizeof ba +
           (int)sizeof sc + (int)sizeof uc +
           (int)sizeof sh + (int)sizeof ush +
           (int)sizeof ui + (int)sizeof ul +
           (int)sizeof f +
           (int)sizeof i8 + (int)sizeof u8 +
           (int)sizeof i16 + (int)sizeof u16 +
            (int)sizeof i32 + (int)sizeof u32 +
            (int)sizeof li8 + (int)sizeof lu8 +
            (int)sizeof li16 + (int)sizeof lu16 +
            (int)sizeof li32 + (int)sizeof lu32 +
            (int)sizeof fi8 + (int)sizeof fu8 +
            (int)sizeof fi16 + (int)sizeof fu16 +
            (int)sizeof fi32 + (int)sizeof fu32 +
            (int)sizeof imax + (int)sizeof umax;
}

static int vla_sizeof_c99_type_counts(int n)
{
    _Bool b[n];
    bool ba[n];
    signed char sc[n];
    unsigned char uc[n];
    short sh[n];
    unsigned short ush[n];
    unsigned int ui[n];
    unsigned long ul[n];
    float f[n];
    int8_t i8[n];
    uint8_t u8[n];
    int16_t i16[n];
    uint16_t u16[n];
    int32_t i32[n];
    uint32_t u32[n];
        int_least8_t li8[n];
        uint_least8_t lu8[n];
        int_least16_t li16[n];
        uint_least16_t lu16[n];
        int_least32_t li32[n];
        uint_least32_t lu32[n];
        int_fast8_t fi8[n];
        uint_fast8_t fu8[n];
        int_fast16_t fi16[n];
        uint_fast16_t fu16[n];
        int_fast32_t fi32[n];
        uint_fast32_t fu32[n];
        intmax_t imax[n];
        uintmax_t umax[n];
    return (int)(sizeof b / sizeof b[0]) +
           (int)(sizeof ba / sizeof ba[0]) +
           (int)(sizeof sc / sizeof sc[0]) +
           (int)(sizeof uc / sizeof uc[0]) +
           (int)(sizeof sh / sizeof sh[0]) +
           (int)(sizeof ush / sizeof ush[0]) +
           (int)(sizeof ui / sizeof ui[0]) +
           (int)(sizeof ul / sizeof ul[0]) +
           (int)(sizeof f / sizeof f[0]) +
           (int)(sizeof i8 / sizeof i8[0]) +
           (int)(sizeof u8 / sizeof u8[0]) +
           (int)(sizeof i16 / sizeof i16[0]) +
           (int)(sizeof u16 / sizeof u16[0]) +
            (int)(sizeof i32 / sizeof i32[0]) +
            (int)(sizeof u32 / sizeof u32[0]) +
            (int)(sizeof li8 / sizeof li8[0]) +
            (int)(sizeof lu8 / sizeof lu8[0]) +
            (int)(sizeof li16 / sizeof li16[0]) +
            (int)(sizeof lu16 / sizeof lu16[0]) +
            (int)(sizeof li32 / sizeof li32[0]) +
            (int)(sizeof lu32 / sizeof lu32[0]) +
            (int)(sizeof fi8 / sizeof fi8[0]) +
            (int)(sizeof fu8 / sizeof fu8[0]) +
            (int)(sizeof fi16 / sizeof fi16[0]) +
            (int)(sizeof fu16 / sizeof fu16[0]) +
            (int)(sizeof fi32 / sizeof fi32[0]) +
            (int)(sizeof fu32 / sizeof fu32[0]) +
            (int)(sizeof imax / sizeof imax[0]) +
            (int)(sizeof umax / sizeof umax[0]);
}

/* The VLA bound (with a side effect) is evaluated exactly once; sizeof reads
 * the stored size afterwards rather than re-evaluating the bound. */
static int vla_sizeof_saved_once(int n)
{
    int calls = n;
    int a[calls++];
    /* count is n (calls++ used n); calls is n+1 iff evaluated exactly once. */
    return (int)(sizeof a / sizeof a[0]) * 1000 + calls;
}

/* sizeof as the byte count of a memset over the whole VLA. */
static int vla_memset_sizeof(int n)
{
    int a[n];
    int i;
    int s = 0;
    memset(a, 0, sizeof a);
    for (i = 0; i < n; i++)
        s += a[i];
    return s;                           /* 0 */
}

/* --- sizeof of a VLA declared in a NESTED scope ---
 * A nested-block declaration only enters the symbol table when its span is
 * emitted, so sizeof must be resolved at emit time.  These would silently
 * return the wrong size if sizeof were baked at AST-build time. */
static int vla_sizeof_nested_block(int n)
{
    int r = 0;
    {
        int a[n];
        r = (int)(sizeof a / sizeof a[0]);      /* n */
    }
    return r;
}

static int vla_sizeof_if_body(int n)
{
    int r = -1;
    if (n > 0) {
        int a[n];
        r = (int)(sizeof a / sizeof a[0]);      /* n */
    }
    return r;
}

static int vla_sizeof_deep_nested(int n)
{
    {
        {
            {
                int a[n];
                return (int)(sizeof a / sizeof a[0]);   /* n */
            }
        }
    }
}

/* A VLA re-declared each loop iteration with a changing bound: sizeof must
 * reflect the current iteration's size, not a stale one. */
static int vla_sizeof_loop_changes(int n)
{
    int i;
    int acc = 0;
    for (i = 1; i <= n; i++) {
        int a[i];
        acc += (int)(sizeof a / sizeof a[0]);   /* += i */
    }
    return acc;                                 /* 1+2+...+n */
}

/* sizeof of the FIRST VLA taken after a SECOND VLA is declared: exercises the
 * per-VLA hidden size-slot offsets staying distinct and correct. */
static int vla_sizeof_first_after_second(int n)
{
    int a[n];
    int b[n + 5];
    int cb = (int)(sizeof b / sizeof b[0]);     /* n+5 */
    int ca = (int)(sizeof a / sizeof a[0]);     /* n */
    return ca * 1000 + cb;
}

static int vla_sizeof_shadow_inner(int n)
{
    int a[3];
    {
        int a[n];
        return (int)(sizeof a / sizeof a[0]);   /* inner VLA */
    }
}

static int vla_sizeof_shadow_outer_after(int n)
{
    int a[n];
    {
        int a[4];
        (void)a;
    }
    return (int)(sizeof a / sizeof a[0]);       /* outer VLA */
}

/* --- sizeof of a VLA as an operand of arithmetic/bitwise operators ---
 * A whole-VLA sizeof is a run-time value, so these must NOT be folded as if
 * sizeof were a compile-time constant (which would read a stale immediate). */
static int vla_sizeof_op_sub(int n)   { int a[n]; return (int)(sizeof a - 2); }
static int vla_sizeof_op_add(int n)   { int a[n]; return (int)(sizeof a + 1); }
static int vla_sizeof_op_and(int n)   { int a[n]; return (int)(sizeof a & 7); }
static int vla_sizeof_op_mullhs(int n){ int a[n]; return (int)(3 * sizeof a); }
static int vla_sizeof_op_mulrhs(int n){ int a[n]; return (int)(sizeof a * 3); }
static int vla_sizeof_op_cmp(int n)   { int a[n]; return sizeof a == (unsigned)(n * sizeof(int)); }

/* sizeof of a VLA used as a subscript index (run-time value). */
static int vla_sizeof_subscript(int n)
{
    static int table[64];
    int a[n];
    int i;
    for (i = 0; i < 64; i++)
        table[i] = i + 1;
    return table[sizeof a];             /* table[n*sizeof(int)] = n*sizeof(int)+1 */
}

/* sizeof of a VLA in ternary / comma / loop-condition contexts. */
static int vla_sizeof_ternary(int n)
{
    int a[n];
    return (sizeof a > sizeof a[0]) ? (int)(sizeof a / sizeof a[0]) : -1;   /* n for n>1 */
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

static int vla_ptr2d_deref_chain(int rows)
{
    int grid[rows][3];
    int (*p)[3] = grid;
    int i, j, s = 0;
    for (i = 0; i < rows; i++)
        for (j = 0; j < 3; j++)
            *(*(p + i) + j) = i * 10 + j;
    for (i = 0; i < rows; i++)
        for (j = 0; j < 3; j++)
            s += *(*(p + i) + j);
    return s;
}

static int vla_ptr2d_deref_chain_while(int rows)
{
    int grid[rows][3];
    int (*p)[3] = grid;
    int i, j, s = 0;
    i = 0;
    while (i < rows) {
        j = 0;
        while (j < 3) {
            *(*(p + i) + j) = i * 10 + j;
            j++;
        }
        i++;
    }
    i = 0;
    while (i < rows) {
        j = 0;
        while (j < 3) {
            s += *(*(p + i) + j);
            j++;
        }
        i++;
    }
    return s;
}

/* Three-dimensional pointer-to-array dereference chain: the explicit
 * *(*(*(p + i) + j) + k) desugaring of p[i][j][k], written for both a store
 * and a read, must match the bracket form. */
static int vla_ptr3d_deref_chain(int rows)
{
    int cube[rows][2][3];
    int (*p)[2][3] = cube;
    int i, j, k, s = 0;
    for (i = 0; i < rows; i++)
        for (j = 0; j < 2; j++)
            for (k = 0; k < 3; k++)
                *(*(*(p + i) + j) + k) = i * 100 + j * 10 + k;
    for (i = 0; i < rows; i++)
        for (j = 0; j < 2; j++)
            for (k = 0; k < 3; k++)
                s += *(*(*(p + i) + j) + k);
    return s;
}

static int vla_ptr3d_deref_chain_do(int rows)
{
    int cube[rows][2][3];
    int (*p)[2][3] = cube;
    int i, j, k, s = 0;
    i = 0;
    do {
        j = 0;
        do {
            k = 0;
            do {
                *(*(*(p + i) + j) + k) = i * 100 + j * 10 + k;
                k++;
            } while (k < 3);
            j++;
        } while (j < 2);
        i++;
    } while (i < rows);
    i = 0;
    do {
        j = 0;
        do {
            k = 0;
            do {
                s += *(*(*(p + i) + j) + k);
                k++;
            } while (k < 3);
            j++;
        } while (j < 2);
        i++;
    } while (i < rows);
    return s;
}

static int vla_ptr2d_deref_chain_return(int rows)
{
    int grid[rows][3];
    int (*p)[3] = grid;
    *(*(p + 1) + 2) = 77;
    return *(*(p + 1) + 2);
}

static int vla_ptr2d_deref_chain_switch(int rows, int (*p)[3])
{
    switch (rows) {
    case 2:
        *(*(p + 0) + 1) = 20;
        break;
    default:
        *(*(p + 0) + 1) = 99;
        break;
    }
    return *(*(p + 0) + 1);
}

static int vla_ptr2d_deref_chain_contexts(int rows)
{
    int grid[rows][3];
    int (*p)[3] = grid;
    int sw;

    if (rows == 2)
        *(*(p + 0) + 0) = 10;

    sw = vla_ptr2d_deref_chain_switch(rows, p);

    *(*(p + 0) + 2) = 30;
    {
        int x = *(*(p + 0) + 2);
        return *(*(p + 0) + 0) + sw + x +
               vla_ptr2d_deref_chain_return(rows);
    }
}

static int vla_ptr2d_deref_chain_compound(int rows)
{
    int grid[rows][3];
    int (*p)[3] = grid;
    *(*(p + 1) + 2) = 40;
    *(*(p + 1) + 2) += 2;
    return *(*(p + 1) + 2);
}

/* Storing a long-typed RHS into an int VLA element uses the truncating element
 * store path (the RHS is long because the literal 1000L is long, forcing long
 * arithmetic).  Values are kept within 16 bits so the result is identical on a
 * 16-bit-int target and a 32-bit host baseline. */
static int vla_long_rhs_store(int n)
{
    int a[n];
    int i, s = 0;
    for (i = 0; i < n; i++)
        a[i] = 1000L + i;
    for (i = 0; i < n; i++)
        s += a[i];
    return s;                           /* sum(1000 + i) for i in [0, n) */
}

/* Ten-dimensional VLA (variable first dimension, constant inner dims) accessed
 * through a full ten-level explicit dereference chain.  Confirms the chain
 * lowering is fully generalised well beyond 2-D/3-D: C99/C11 5.2.4.1 guarantees
 * support for at least 12 array declarators, and dcc caps rank at 12. */
static int vla_ptr10d_deref_chain(int rows)
{
    int cube[rows][2][2][2][2][2][2][2][2][2];
    int (*p)[2][2][2][2][2][2][2][2][2] = cube;
    int a, b, c, d, e, f, g, h, i, j, s = 0;
    for (a = 0; a < rows; a++)
     for (b = 0; b < 2; b++) for (c = 0; c < 2; c++) for (d = 0; d < 2; d++)
      for (e = 0; e < 2; e++) for (f = 0; f < 2; f++) for (g = 0; g < 2; g++)
       for (h = 0; h < 2; h++) for (i = 0; i < 2; i++) for (j = 0; j < 2; j++)
        *(*(*(*(*(*(*(*(*(*(p + a) + b) + c) + d) + e) + f) + g) + h) + i) + j) = 1;
    for (a = 0; a < rows; a++)
     for (b = 0; b < 2; b++) for (c = 0; c < 2; c++) for (d = 0; d < 2; d++)
      for (e = 0; e < 2; e++) for (f = 0; f < 2; f++) for (g = 0; g < 2; g++)
       for (h = 0; h < 2; h++) for (i = 0; i < 2; i++) for (j = 0; j < 2; j++)
        s += *(*(*(*(*(*(*(*(*(*(p + a) + b) + c) + d) + e) + f) + g) + h) + i) + j);
    return s;                           /* rows * 2^9 */
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

    /* An unused VLA must not be pruned (would clobber the saved SP). */
    check_int("unused_vla_prune_fallthrough",
              unused_vla_prune_fallthrough(4), 22223);
    check_int("unused_vla_prune_locals",
              unused_vla_prune_locals(4), 666);
    check_int("unused_vla_prune_sp_alias",
              unused_vla_prune_sp_alias(4), 1234);
    check_int("unused_vla_prune_same_decl",
              unused_vla_prune_same_decl(4), 4321);

    /* sizeof on a whole VLA yields its run-time size; expectations are written
     * via sizeof(int) so they hold on both a 16-bit and a 32-bit int target. */
    check_int("vla_sizeof_1d", vla_sizeof_1d(5), 5 * (int)sizeof(int));
    check_int("vla_sizeof_2d", vla_sizeof_2d(4), 4 * 3 * (int)sizeof(int));
    check_int("vla_sizeof_element", vla_sizeof_element(9), (int)sizeof(int));
    check_int("vla_sizeof_count", vla_sizeof_count(7), 7);
    check_int("vla_sizeof_2d_rows", vla_sizeof_2d_rows(4), 4);
    check_int("vla_sizeof_2d_row", vla_sizeof_2d_row(4), 3 * (int)sizeof(int));
    check_int("vla_sizeof_char", vla_sizeof_char(6), 6);
    check_int("vla_sizeof_c99_type_bytes", vla_sizeof_c99_type_bytes(3),
              3 * ((int)sizeof(_Bool) + (int)sizeof(bool) +
                   (int)sizeof(signed char) + (int)sizeof(unsigned char) +
                   (int)sizeof(short) + (int)sizeof(unsigned short) +
                   (int)sizeof(unsigned int) + (int)sizeof(unsigned long) +
                   (int)sizeof(float) +
                   (int)sizeof(int8_t) + (int)sizeof(uint8_t) +
                   (int)sizeof(int16_t) + (int)sizeof(uint16_t) +
                   (int)sizeof(int32_t) + (int)sizeof(uint32_t) +
                   (int)sizeof(int_least8_t) + (int)sizeof(uint_least8_t) +
                   (int)sizeof(int_least16_t) + (int)sizeof(uint_least16_t) +
                   (int)sizeof(int_least32_t) + (int)sizeof(uint_least32_t) +
                   (int)sizeof(int_fast8_t) + (int)sizeof(uint_fast8_t) +
                   (int)sizeof(int_fast16_t) + (int)sizeof(uint_fast16_t) +
                   (int)sizeof(int_fast32_t) + (int)sizeof(uint_fast32_t) +
                   (int)sizeof(intmax_t) + (int)sizeof(uintmax_t)));
    check_int("vla_sizeof_c99_type_counts", vla_sizeof_c99_type_counts(3), 87);
    check_int("vla_sizeof_saved_once", vla_sizeof_saved_once(5), 5 * 1000 + 6);
    check_int("vla_memset_sizeof", vla_memset_sizeof(6), 0);
    /* nested-scope VLA sizeof (resolved at emit time, not AST-build time) */
    check_int("vla_sizeof_nested_block", vla_sizeof_nested_block(6), 6);
    check_int("vla_sizeof_if_body", vla_sizeof_if_body(6), 6);
    check_int("vla_sizeof_deep_nested", vla_sizeof_deep_nested(6), 6);
    check_int("vla_sizeof_loop_changes", vla_sizeof_loop_changes(5), 15);
    check_int("vla_sizeof_first_after_second", vla_sizeof_first_after_second(6), 6 * 1000 + 11);
    check_int("vla_sizeof_shadow_inner", vla_sizeof_shadow_inner(6), 6);
    check_int("vla_sizeof_shadow_outer_after", vla_sizeof_shadow_outer_after(6), 6);
    /* sizeof-of-VLA as an operand: must use the run-time value, not a stale
     * folded immediate. */
    check_int("vla_sizeof_op_sub", vla_sizeof_op_sub(6), 6 * (int)sizeof(int) - 2);
    check_int("vla_sizeof_op_add", vla_sizeof_op_add(6), 6 * (int)sizeof(int) + 1);
    check_int("vla_sizeof_op_and", vla_sizeof_op_and(6), (6 * (int)sizeof(int)) & 7);
    check_int("vla_sizeof_op_mullhs", vla_sizeof_op_mullhs(6), 3 * 6 * (int)sizeof(int));
    check_int("vla_sizeof_op_mulrhs", vla_sizeof_op_mulrhs(6), 6 * (int)sizeof(int) * 3);
    check_int("vla_sizeof_op_cmp", vla_sizeof_op_cmp(6), 1);
    check_int("vla_sizeof_subscript", vla_sizeof_subscript(6), 6 * (int)sizeof(int) + 1);
    check_int("vla_sizeof_ternary", vla_sizeof_ternary(6), 6);

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
    check_int("vla_ptr2d_deref_chain", vla_ptr2d_deref_chain(4), 192);
    check_int("vla_ptr2d_deref_chain_while", vla_ptr2d_deref_chain_while(4), 192);
    check_int("vla_ptr3d_deref_chain", vla_ptr3d_deref_chain(3), 1908);
    check_int("vla_ptr3d_deref_chain_do", vla_ptr3d_deref_chain_do(3), 1908);
    check_int("vla_ptr2d_deref_chain_contexts", vla_ptr2d_deref_chain_contexts(2), 137);
    check_int("vla_ptr2d_deref_chain_compound", vla_ptr2d_deref_chain_compound(2), 42);
    check_int("vla_ptr10d_deref_chain", vla_ptr10d_deref_chain(2), 1024);
    check_int("vla_long_rhs_store", vla_long_rhs_store(5), 5010);

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
