#include <stdio.h>
#include <stdlib.h>

#ifdef QS_EDGE_UNSIGNED_ARRAY
typedef unsigned int qsort_edge_value_t;
#elif defined(QS_EDGE_LONG_ARRAY)
typedef long qsort_edge_value_t;
#else
typedef int qsort_edge_value_t;
#endif

static int failures;
#ifdef QS_EDGE_VOLATILE_ARRAY
static volatile qsort_edge_value_t qsort_edge_work[64];
#else
static qsort_edge_value_t qsort_edge_work[
#ifdef QS_EDGE_ARRAY_65
    65
#else
    64
#endif
];
#endif

#ifdef QS_EDGE_SECOND_ARRAY
static qsort_edge_value_t qsort_edge_other[64];
#endif

static void qsort_edge_fail(const char *message)
{
    printf("FAIL %s\n", message);
    ++failures;
}

static int qsort_edge_compare(const void *left, const void *right)
{
    qsort_edge_value_t a = *(const qsort_edge_value_t *)left;
    qsort_edge_value_t b = *(const qsort_edge_value_t *)right;

    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return 0;
}

#ifdef QS_EDGE_ALT_COMPARE
static int alternate_compare_calls;

static int qsort_edge_alternate_compare(
    const void *left, const void *right)
{
    ++alternate_compare_calls;
    return qsort_edge_compare(left, right);
}
#endif

#ifdef QS_EDGE_ALT_FAILURE
static void qsort_edge_alternate_fail(const char *message)
{
    ++failures;
    qsort_edge_fail(message);
}
#endif

#ifdef QS_EDGE_RENAMED
#define QS_EDGE_FUNCTION fixture_qsort_edge_renamed
#else
#define QS_EDGE_FUNCTION fixture_qsort_edge
#endif

static void QS_EDGE_FUNCTION(void)
{
#ifdef QS_EDGE_VOLATILE_INDEX
    volatile int i;
#elif defined(QS_EDGE_UNSIGNED_INDEX)
    unsigned int i;
#else
    int i;
#endif

    qsort_edge_work[0] = 0x55;
    qsort(
        (void *)qsort_edge_work, 0U, sizeof(qsort_edge_value_t),
        qsort_edge_compare);
    if (qsort_edge_work[0] != 0x55)
        qsort_edge_fail("qsort n=0 disturbed memory");

    qsort_edge_work[0] = 42;
    qsort(
        (void *)qsort_edge_work, 1U, sizeof(qsort_edge_value_t),
        qsort_edge_compare);
    if (qsort_edge_work[0] != 42)
        qsort_edge_fail("qsort n=1 changed value");

    qsort_edge_work[0] = 9;
    qsort_edge_work[1] = 4;
    qsort(
        (void *)qsort_edge_work, 2U, sizeof(qsort_edge_value_t),
        qsort_edge_compare);
    if (qsort_edge_work[0] != 4 || qsort_edge_work[1] != 9)
        qsort_edge_fail("qsort n=2 unsorted->sorted");
    qsort_edge_work[0] = 1;
    qsort_edge_work[1] = 7;
    qsort(
        (void *)qsort_edge_work, 2U, sizeof(qsort_edge_value_t),
        qsort_edge_compare);
    if (qsort_edge_work[0] != 1 || qsort_edge_work[1] != 7)
        qsort_edge_fail("qsort n=2 sorted stays");

    for (i = 0; i < 20; ++i)
        qsort_edge_work[i] = i;
    qsort(
        (void *)qsort_edge_work, 20U, sizeof(qsort_edge_value_t),
        qsort_edge_compare);
    for (i = 0; i < 20; ++i)
        if (qsort_edge_work[i] != i)
            qsort_edge_fail("qsort already-sorted");

    for (i = 0; i < 20; ++i)
        qsort_edge_work[i] = 19 - i;
    qsort(
        (void *)qsort_edge_work, 20U, sizeof(qsort_edge_value_t),
        qsort_edge_compare);
    for (i = 0; i < 20; ++i)
        if (qsort_edge_work[i] != i)
            qsort_edge_fail("qsort reverse-sorted");

#ifdef QS_EDGE_SECOND_ARRAY
#define QS_EDGE_FINAL_ARRAY qsort_edge_other
#else
#define QS_EDGE_FINAL_ARRAY qsort_edge_work
#endif
    for (i = 0; i < 20; ++i)
        QS_EDGE_FINAL_ARRAY[i] = 7;
    qsort(
        (void *)QS_EDGE_FINAL_ARRAY, 20U, sizeof(qsort_edge_value_t),
#ifdef QS_EDGE_ALT_COMPARE
        qsort_edge_alternate_compare);
#else
        qsort_edge_compare);
#endif
    for (i = 0; i < 20; ++i)
        if (QS_EDGE_FINAL_ARRAY[i] != 7)
#ifdef QS_EDGE_ALT_FAILURE
            qsort_edge_alternate_fail("qsort all-equal");
#else
            qsort_edge_fail("qsort all-equal");
#endif
#ifdef QS_EDGE_EXTRA_CFG
    if (failures == 123)
        qsort_edge_work[0] = 99;
#endif
}

int main(void)
{
    QS_EDGE_FUNCTION();
#ifdef QS_EDGE_ALT_COMPARE
    if (alternate_compare_calls == 0)
        ++failures;
#endif
#ifdef QS_EDGE_SECOND_ARRAY
    if (qsort_edge_other[0] != 7)
        ++failures;
#endif
    printf("qsort edge failures=%d\n", failures);
    return failures != 0;
}
