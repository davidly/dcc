/* tbsearch.c - comprehensive unit tests for the runtime bsearch.
 *
 * bsearch is exercised over a sorted int array (every present key, every
 * absent gap key, below-min/above-max sentinels, first/last elements, and the
 * empty, single-element, and two-element edges) and over a wide struct array.
 * The comparator is called through the runtime indirect-call path.
 *
 * The test prints diagnostics only on failure and a single success line, so the
 * regression baseline stays stable.
 */

#include <stdio.h>
#include <stdlib.h>

static int fails;

static void fail(const char *msg)
{
    printf("FAIL %s\n", msg);
    fails++;
}

static int cmp_int_asc(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;

    if (x < y)
        return -1;
    if (x > y)
        return 1;
    return 0;
}

struct rec {
    int key;
    long pad;       /* makes sizeof(struct rec) == 6: a wide element */
};

static int cmp_rec(const void *a, const void *b)
{
    int ka = ((const struct rec *)a)->key;
    int kb = ((const struct rec *)b)->key;

    if (ka < kb)
        return -1;
    if (ka > kb)
        return 1;
    return 0;
}

/* ============================================================ bsearch: int */
static int sorted[51];

static void t_bsearch_int(void)
{
    int n;
    int i;
    int key;
    const int *r;

    /* sorted array of even numbers 0,2,4,...,100 (odd numbers are absent) */
    n = 51;
    for (i = 0; i < n; i++)
        sorted[i] = i * 2;

    /* every present key is found and points at the right element */
    for (i = 0; i < n; i++) {
        key = i * 2;
        r = (const int *)bsearch(&key, sorted, (size_t)n, sizeof(int), cmp_int_asc);
        if (r == 0 || *r != key)
            fail("bsearch missed present key");
    }

    /* odd numbers (gaps) are absent */
    for (i = 0; i < n - 1; i++) {
        key = i * 2 + 1;
        r = (const int *)bsearch(&key, sorted, (size_t)n, sizeof(int), cmp_int_asc);
        if (r != 0)
            fail("bsearch found absent gap key");
    }

    /* below minimum and above maximum */
    key = -1;
    if (bsearch(&key, sorted, (size_t)n, sizeof(int), cmp_int_asc) != 0)
        fail("bsearch found below-min key");
    key = 101;
    if (bsearch(&key, sorted, (size_t)n, sizeof(int), cmp_int_asc) != 0)
        fail("bsearch found above-max key");

    /* first and last specifically */
    key = 0;
    r = (const int *)bsearch(&key, sorted, (size_t)n, sizeof(int), cmp_int_asc);
    if (r == 0 || *r != 0)
        fail("bsearch first element");
    key = 100;
    r = (const int *)bsearch(&key, sorted, (size_t)n, sizeof(int), cmp_int_asc);
    if (r == 0 || *r != 100)
        fail("bsearch last element");
}

static void t_bsearch_edges(void)
{
    int key;
    const int *r;

    /* empty array: always not found */
    key = 0;
    if (bsearch(&key, sorted, 0U, sizeof(int), cmp_int_asc) != 0)
        fail("bsearch empty array");

    /* single element: hit and miss either side */
    sorted[0] = 55;
    key = 55;
    r = (const int *)bsearch(&key, sorted, 1U, sizeof(int), cmp_int_asc);
    if (r == 0 || *r != 55)
        fail("bsearch single hit");
    key = 54;
    if (bsearch(&key, sorted, 1U, sizeof(int), cmp_int_asc) != 0)
        fail("bsearch single miss low");
    key = 56;
    if (bsearch(&key, sorted, 1U, sizeof(int), cmp_int_asc) != 0)
        fail("bsearch single miss high");

    /* two elements: both hits, plus low/middle-gap/high misses */
    sorted[0] = 10;
    sorted[1] = 20;
    key = 10;
    r = (const int *)bsearch(&key, sorted, 2U, sizeof(int), cmp_int_asc);
    if (r == 0 || *r != 10)
        fail("bsearch two-elem first");
    key = 20;
    r = (const int *)bsearch(&key, sorted, 2U, sizeof(int), cmp_int_asc);
    if (r == 0 || *r != 20)
        fail("bsearch two-elem second");
    key = 5;
    if (bsearch(&key, sorted, 2U, sizeof(int), cmp_int_asc) != 0)
        fail("bsearch two-elem miss low");
    key = 15;
    if (bsearch(&key, sorted, 2U, sizeof(int), cmp_int_asc) != 0)
        fail("bsearch two-elem miss gap");
    key = 25;
    if (bsearch(&key, sorted, 2U, sizeof(int), cmp_int_asc) != 0)
        fail("bsearch two-elem miss high");
}

/* ================================================= bsearch: wide comparator */
/* Regression coverage for a real runtime bug: the comparator result's sign
 * was tested via the Z80 flag left by "ld a,h / or l" (OR of the two bytes),
 * which is NOT the sign bit of the 16-bit result whenever the low byte's own
 * bit 7 is set while the high byte is 0 - e.g. +128 (0x0080) looks negative
 * that way. cmp_int_asc above only ever returns -1/0/1, so it can't land in
 * that range; this comparator returns the raw difference, and the array is
 * spaced far enough apart that ordinary present-key lookups produce
 * comparator results throughout (and beyond) the 128..255 danger zone. */
static int wide[10];

static int cmp_diff(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;

    return x - y;
}

static void t_bsearch_wide_comparator(void)
{
    int i;
    int key;
    const int *r;

    for (i = 0; i < 10; i++)
        wide[i] = i * 70;      /* 0,70,140,...,630 */

    for (i = 0; i < 10; i++) {
        key = i * 70;
        r = (const int *)bsearch(&key, wide, (size_t)10, sizeof(int), cmp_diff);
        if (r == 0 || *r != key)
            fail("bsearch wide-comparator missed present key");
    }
    for (i = 0; i < 9; i++) {
        key = i * 70 + 35;
        r = (const int *)bsearch(&key, wide, (size_t)10, sizeof(int), cmp_diff);
        if (r != 0)
            fail("bsearch wide-comparator found absent gap key");
    }
    key = -35;
    if (bsearch(&key, wide, (size_t)10, sizeof(int), cmp_diff) != 0)
        fail("bsearch wide-comparator found below-min key");
    key = 665;
    if (bsearch(&key, wide, (size_t)10, sizeof(int), cmp_diff) != 0)
        fail("bsearch wide-comparator found above-max key");
}

/* ============================================================ bsearch: struct */
static struct rec recs[20];

static void t_bsearch_struct(void)
{
    int i;
    struct rec key;
    const struct rec *r;

    for (i = 0; i < 20; i++) {
        recs[i].key = i * 5;        /* 0,5,10,...,95 */
        recs[i].pad = (long)i;
    }
    for (i = 0; i < 20; i++) {
        key.key = i * 5;
        key.pad = 0;
        r = (const struct rec *)bsearch(&key, recs, 20U, sizeof(struct rec), cmp_rec);
        if (r == 0 || r->key != i * 5)
            fail("bsearch struct present");
    }
    key.key = 7;        /* not a multiple of 5 */
    if (bsearch(&key, recs, 20U, sizeof(struct rec), cmp_rec) != 0)
        fail("bsearch struct absent");
}

int main(void)
{
    fails = 0;

    t_bsearch_int();
    t_bsearch_edges();
    t_bsearch_wide_comparator();
    t_bsearch_struct();

    if (fails != 0) {
        printf("tbsearch FAILED: %d\n", fails);
        return 1;
    }
    printf("tbsearch: all tests passed\n");
    return 0;
}
