/* tqsort.c - comprehensive unit tests for the runtime qsort.
 *
 * qsort is exercised across multiple element sizes (int, a 6-byte struct, a
 * 5-byte odd-sized record, single bytes, char* pointers, and large records)
 * and comparator directions, with results checked against an independent
 * insertion-sort oracle, plus the empty/single/sorted/reverse/all-equal edge
 * cases.
 * (bsearch is covered by tbsearch.c and atol by tstdlib.c.)
 *
 * The test prints diagnostics only on failure and a single success line, so the
 * regression baseline stays stable.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void fail(const char *msg)
{
    printf("FAIL %s\n", msg);
    fails++;
}

/* ---- deterministic PRNG ---- */
static unsigned int seed = 0x1234U;

static unsigned int rnd(void)
{
    seed = (unsigned int)(seed * 25173U + 13849U);
    return seed;
}

/* ---- comparators ---- */
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

static int cmp_int_desc(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;

    if (x < y)
        return 1;
    if (x > y)
        return -1;
    return 0;
}

struct rec {
    int key;
    long pad;       /* makes sizeof(struct rec) == 6: exercises a wide element */
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

struct r5 {
    unsigned char b[5];     /* odd 5-byte element: exercises the byte-swap loop */
};

static int cmp_r5(const void *a, const void *b)
{
    return memcmp(a, b, 5);
}

static int cmp_byte(const void *a, const void *b)
{
    int x = *(const unsigned char *)a;
    int y = *(const unsigned char *)b;

    return x - y;
}

static int cmp_str(const void *a, const void *b)
{
    char *sa = *(char **)a;
    char *sb = *(char **)b;

    return strcmp(sa, sb);
}

/* ---- int oracle: insertion sort (obviously correct) ---- */
static void oracle_sort(int *a, int n)
{
    int i;
    int j;
    int key;

    for (i = 1; i < n; i++) {
        key = a[i];
        j = i - 1;
        while (j >= 0 && a[j] > key) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = key;
    }
}

/*
 * Six 8K records make the initial three-record Shell gap 24K.  At the low
 * static-data address used by a CP/M .COM, subtracting that gap from record
 * zero wraps onto record five.  The extra pair is observable without touching
 * an invalid address, and the following walk is rejected by the comparator.
 *
 * This regression only exists at the low, 16-bit-wrapping static address a
 * CP/M .COM loads at; on host, large_records sits in a 32/64-bit address
 * space with no wraparound risk, so large_wrap_pairs there just reflects
 * whichever pivot order the host libc's own qsort() happens to pick for
 * this input - not a wraparound bug. Host-skipped for that reason (see
 * tests/_test_overrides.json).
 */
#define LARGE_COUNT 6
#define LARGE_SIZE 8192U
#define LARGE_BYTES 49152U

static unsigned char large_records[LARGE_BYTES];
static int large_wrap_pairs;
static int large_wrap_swaps;
static int large_bad_pointers;

static unsigned char *large_record(int i)
{
    return large_records + (unsigned int)i * LARGE_SIZE;
}

static int large_record_index(const void *p)
{
    int i;

    for (i = 0; i < LARGE_COUNT; i++)
        if (p == (const void *)large_record(i))
            return i;
    return -1;
}

static int large_key(const unsigned char *p)
{
    return (int)p[0] + ((int)p[1] << 8);
}

static int cmp_large(const void *a, const void *b)
{
    int ia = large_record_index(a);
    int ib = large_record_index(b);
    int ka;
    int kb;

    if (ia < 0 || ib < 0) {
        large_bad_pointers++;
        return 0;
    }

    ka = large_key((const unsigned char *)a);
    kb = large_key((const unsigned char *)b);

    if (ia == 5 && ib == 0)
        large_wrap_pairs++;
    if (ia == 2 && ib == 5 && large_key(large_record(0)) == 5 &&
            large_key(large_record(5)) == 1)
        large_wrap_swaps++;

    if (ka < kb)
        return -1;
    if (ka > kb)
        return 1;
    return 0;
}

static void t_qsort_large_gap(void)
{
    static const int input[LARGE_COUNT] = { 4, 3, 2, 1, 0, 5 };
    int expected[LARGE_COUNT];
    unsigned char *p;
    unsigned char fill;
    int content_bad;
    int i;
    unsigned int j;

    large_wrap_pairs = 0;
    large_wrap_swaps = 0;
    large_bad_pointers = 0;

    for (i = 0; i < LARGE_COUNT; i++) {
        expected[i] = input[i];
        p = large_record(i);
        fill = (unsigned char)(0x40 + input[i]);
        memset(p, fill, LARGE_SIZE);
        p[0] = (unsigned char)input[i];
        p[1] = 0;
    }
    oracle_sort(expected, LARGE_COUNT);

    qsort(large_records, LARGE_COUNT, LARGE_SIZE, cmp_large);

    for (i = 0; i < LARGE_COUNT; i++)
        if (large_key(large_record(i)) != expected[i]) {
            fail("qsort large-gap order");
            break;
        }

    content_bad = 0;
    for (i = 0; i < LARGE_COUNT && !content_bad; i++) {
        p = large_record(i);
        fill = (unsigned char)(0x40 + expected[i]);
        for (j = 2U; j < LARGE_SIZE; j++)
            if (p[j] != fill) {
                content_bad = 1;
                break;
            }
    }
    if (content_bad)
        fail("qsort large-gap content");
    if (large_wrap_pairs != 0)
        fail("qsort large-gap wrapped comparator pair");
    if (large_wrap_swaps != 0)
        fail("qsort large-gap wrapped swap");
    if (large_bad_pointers != 0)
        fail("qsort large-gap invalid comparator pointer");
}

/* ============================================================ qsort: ints */
static int work[64];
static int copy[64];

static void t_qsort_int(void)
{
    int n;
    int i;
    int pass;

    /* many random arrays of varying length vs the insertion-sort oracle */
    for (pass = 0; pass < 40; pass++) {
        n = (int)(rnd() % 64U);             /* 0 .. 63, includes 0 and 1 */
        for (i = 0; i < n; i++) {
            work[i] = (int)(rnd() % 200U) - 100;    /* -100 .. 99, dup-rich */
            copy[i] = work[i];
        }
        oracle_sort(copy, n);
        qsort(work, (size_t)n, sizeof(int), cmp_int_asc);
        for (i = 0; i < n; i++)
            if (work[i] != copy[i])
                fail("qsort int != oracle");
    }
}

static void t_qsort_int_edges(void)
{
    int i;

    /* n == 0 must not touch memory or crash */
    work[0] = 0x55;
    qsort(work, 0U, sizeof(int), cmp_int_asc);
    if (work[0] != 0x55)
        fail("qsort n=0 disturbed memory");

    /* n == 1 is a no-op */
    work[0] = 42;
    qsort(work, 1U, sizeof(int), cmp_int_asc);
    if (work[0] != 42)
        fail("qsort n=1 changed value");

    /* n == 2 both orders */
    work[0] = 9; work[1] = 4;
    qsort(work, 2U, sizeof(int), cmp_int_asc);
    if (work[0] != 4 || work[1] != 9)
        fail("qsort n=2 unsorted->sorted");
    work[0] = 1; work[1] = 7;
    qsort(work, 2U, sizeof(int), cmp_int_asc);
    if (work[0] != 1 || work[1] != 7)
        fail("qsort n=2 sorted stays");

    /* already sorted */
    for (i = 0; i < 20; i++)
        work[i] = i;
    qsort(work, 20U, sizeof(int), cmp_int_asc);
    for (i = 0; i < 20; i++)
        if (work[i] != i)
            fail("qsort already-sorted");

    /* reverse sorted */
    for (i = 0; i < 20; i++)
        work[i] = 19 - i;
    qsort(work, 20U, sizeof(int), cmp_int_asc);
    for (i = 0; i < 20; i++)
        if (work[i] != i)
            fail("qsort reverse-sorted");

    /* all equal */
    for (i = 0; i < 20; i++)
        work[i] = 7;
    qsort(work, 20U, sizeof(int), cmp_int_asc);
    for (i = 0; i < 20; i++)
        if (work[i] != 7)
            fail("qsort all-equal");
}

static void t_qsort_desc(void)
{
    int i;

    for (i = 0; i < 30; i++)
        work[i] = (int)(rnd() % 500U);
    qsort(work, 30U, sizeof(int), cmp_int_desc);
    for (i = 1; i < 30; i++)
        if (work[i - 1] < work[i])
            fail("qsort descending order");
}

/* ============================================================ qsort: struct */
static struct rec recs[24];

static void t_qsort_struct(void)
{
    int i;

    for (i = 0; i < 24; i++) {
        recs[i].key = (int)(rnd() % 1000U);
        recs[i].pad = (long)i;          /* tag to confirm whole element moves */
    }
    qsort(recs, 24U, sizeof(struct rec), cmp_rec);
    for (i = 1; i < 24; i++)
        if (recs[i - 1].key > recs[i].key)
            fail("qsort struct key order");
    /* pad must still be a permutation of 0..23 (each appears once) */
    {
        int seen[24];
        int k;
        for (i = 0; i < 24; i++)
            seen[i] = 0;
        for (i = 0; i < 24; i++) {
            k = (int)recs[i].pad;
            if (k < 0 || k >= 24)
                fail("qsort struct pad out of range");
            else
                seen[k]++;
        }
        for (i = 0; i < 24; i++)
            if (seen[i] != 1)
                fail("qsort struct lost/duplicated element");
    }
}

/* ============================================================ qsort: 5-byte */
static struct r5 fives[16];

static void t_qsort_r5(void)
{
    int i;
    int j;

    for (i = 0; i < 16; i++)
        for (j = 0; j < 5; j++)
            fives[i].b[j] = (unsigned char)(rnd() & 0xffU);
    qsort(fives, 16U, sizeof(struct r5), cmp_r5);
    for (i = 1; i < 16; i++)
        if (memcmp(fives[i - 1].b, fives[i].b, 5) > 0)
            fail("qsort 5-byte order");
}

/* ============================================================ qsort: bytes */
static unsigned char bytes[64];

static void t_qsort_bytes(void)
{
    int i;

    for (i = 0; i < 64; i++)
        bytes[i] = (unsigned char)(rnd() & 0xffU);
    qsort(bytes, 64U, 1U, cmp_byte);
    for (i = 1; i < 64; i++)
        if (bytes[i - 1] > bytes[i])
            fail("qsort byte order");
}

/* ============================================================ qsort: strings */
static char *strs[8];

static void t_qsort_str(void)
{
    int i;

    strs[0] = "pear";
    strs[1] = "apple";
    strs[2] = "orange";
    strs[3] = "fig";
    strs[4] = "banana";
    strs[5] = "kiwi";
    strs[6] = "grape";
    strs[7] = "cherry";
    qsort(strs, 8U, sizeof(char *), cmp_str);
    for (i = 1; i < 8; i++)
        if (strcmp(strs[i - 1], strs[i]) > 0)
            fail("qsort string order");
    if (strcmp(strs[0], "apple") != 0)
        fail("qsort string first");
    if (strcmp(strs[7], "pear") != 0)
        fail("qsort string last");
}

int main(void)
{
    fails = 0;

    t_qsort_int();
    t_qsort_int_edges();
    t_qsort_large_gap();
    t_qsort_desc();
    t_qsort_struct();
    t_qsort_r5();
    t_qsort_bytes();
    t_qsort_str();

    if (fails != 0) {
        printf("tqsort FAILED: %d\n", fails);
        return 1;
    }
    printf("tqsort: all tests passed\n");
    return 0;
}
