/* Dedicated semantic controls for the fixed wrapper initializer. */

#include <stdio.h>

struct Leaf {
#ifdef WRAP22_PAD_LEAF
    char pad;
#endif
#ifdef WRAP22_VOLATILE_MEMBER
    volatile int v;
#else
    int v;
#endif
    int a[4];
#ifdef WRAP22_UNSIGNED_CHAR
    unsigned char cv;
    unsigned char ca[4];
#else
    char cv;
    char ca[4];
#endif
    long lv;
    long la[4];
};

struct Node {
    struct Leaf leaf[3];
    struct Leaf *pl;
    int *pi;
    char *pc;
    long *plong;
    int m[2][3];
#ifdef WRAP22_UNSIGNED_CHAR
    unsigned char cm[2][3];
#else
    char cm[2][3];
#endif
    long lm[2][3];
};

struct Wrapper {
    struct Node n[2];
    struct Node *pn;
    struct Leaf *lp[3];
    int *ip[4];
    char *cp[4];
    long *longp[4];
};

#ifdef WRAP22_INSERT_CALL
static void touch(p)
struct Wrapper *p;
{
    if (p == (struct Wrapper *)0)
        puts("unexpected");
}
#endif

#ifdef WRAP22_RENAMED_LOCALS
#define w target
#define base seed
#define wi wrapper_index
#define ni node_index
#define li leaf_index
#define k element_index
#define r row_index
#define c column_index
#endif

#ifdef WRAP22_VARIADIC
static void init_wrapper(struct Wrapper *w, int base, ...)
#elif defined(WRAP22_INT_RETURN)
static int init_wrapper(w, base)
#elif defined(WRAP22_POINTER_BASE)
static void init_wrapper(w, basep)
#else
static void init_wrapper(w, base)
#endif
#ifndef WRAP22_VARIADIC
#ifdef WRAP22_VOLATILE_WRAPPER
volatile struct Wrapper *w;
#else
struct Wrapper *w;
#endif
#ifdef WRAP22_POINTER_BASE
int *basep;
#elif defined(WRAP22_WIDE_BASE)
long base;
#else
int base;
#endif
#endif
{
#ifdef WRAP22_POINTER_BASE
    int base = *basep;
#endif
#ifdef WRAP22_STATIC_INDEX
    static int wi;
#else
    int wi;
#endif
    int ni;
    int li;
    int k;
    int r;
    int c;

#ifdef WRAP22_INSERT_CALL
    touch(w);
#endif
#ifdef WRAP22_ALT_BOUND
    for (ni = 0; ni != 2; ni++) {
#else
    for (ni = 0; ni < 2; ni++) {
#endif
        for (li = 0; li < 3; li++) {
#ifdef WRAP22_REORDER_DATAFLOW
            w->n[ni].leaf[li].v = base + li * 10 + ni * 100 + 1;
#else
            w->n[ni].leaf[li].v = base + ni * 100 + li * 10 + 1;
#endif
            w->n[ni].leaf[li].cv = (char)((base + ni * 20 + li * 3 + 2) & 127);
            w->n[ni].leaf[li].lv = (long)base * 1000L + (long)ni * 100L + (long)li * 10L + 3L;
            for (k = 0; k < 4; k++) {
                w->n[ni].leaf[li].a[k] = base + ni * 100 + li * 10 + k + 20;
                w->n[ni].leaf[li].ca[k] = (char)((base + ni * 20 + li * 5 + k + 30) & 127);
                w->n[ni].leaf[li].la[k] = (long)base * 1000L + (long)ni * 100L + (long)li * 10L + (long)k + 40L;
            }
        }
        for (r = 0; r < 2; r++) {
            for (c = 0; c < 3; c++) {
                w->n[ni].m[r][c] = base + ni * 100 + r * 10 + c + 200;
                w->n[ni].cm[r][c] = (char)((base + ni * 20 + r * 5 + c + 60) & 127);
                w->n[ni].lm[r][c] = (long)base * 1000L + (long)ni * 100L + (long)r * 10L + (long)c + 300L;
            }
        }
        w->n[ni].pl = &w->n[ni].leaf[1];
        w->n[ni].pi = &w->n[ni].m[1][0];
        w->n[ni].pc = &w->n[ni].cm[1][0];
        w->n[ni].plong = &w->n[ni].lm[1][0];
    }

    w->pn = &w->n[0];
    for (wi = 0; wi < 3; wi++) {
        w->lp[wi] = &w->n[wi & 1].leaf[wi];
    }
    for (k = 0; k < 4; k++) {
        w->ip[k] = &w->n[k & 1].leaf[k % 3].a[k & 3];
        w->cp[k] = &w->n[k & 1].leaf[k % 3].ca[k & 3];
        w->longp[k] = &w->n[k & 1].leaf[k % 3].la[k & 3];
    }
#ifdef WRAP22_INT_RETURN
    return 0;
#endif
}

#ifdef WRAP22_RENAMED_LOCALS
#undef w
#undef base
#undef wi
#undef ni
#undef li
#undef k
#undef r
#undef c
#endif

int main()
{
    struct Wrapper wrapper;
    int base = 7;
    int failures = 0;
    long hash = 0;
    int ni;
    int li;
    int k;

#ifdef WRAP22_POINTER_BASE
    init_wrapper(&wrapper, &base);
#else
    init_wrapper(&wrapper, base);
#endif
    for (ni = 0; ni < 2; ++ni) {
        for (li = 0; li < 3; ++li) {
            hash += wrapper.n[ni].leaf[li].v;
            hash += wrapper.n[ni].leaf[li].cv;
            hash += wrapper.n[ni].leaf[li].lv;
            for (k = 0; k < 4; ++k) {
                hash += wrapper.n[ni].leaf[li].a[k];
                hash += wrapper.n[ni].leaf[li].ca[k];
                hash += wrapper.n[ni].leaf[li].la[k];
            }
        }
        if (wrapper.n[ni].pl != &wrapper.n[ni].leaf[1])
            ++failures;
        if (wrapper.n[ni].pi != &wrapper.n[ni].m[1][0])
            ++failures;
        if (wrapper.n[ni].pc != &wrapper.n[ni].cm[1][0])
            ++failures;
        if (wrapper.n[ni].plong != &wrapper.n[ni].lm[1][0])
            ++failures;
    }
    if (wrapper.pn != &wrapper.n[0])
        ++failures;
    for (k = 0; k < 3; ++k)
        if (wrapper.lp[k] != &wrapper.n[k & 1].leaf[k])
            ++failures;
    for (k = 0; k < 4; ++k) {
        if (wrapper.ip[k] !=
            &wrapper.n[k & 1].leaf[k % 3].a[k & 3])
            ++failures;
        if (wrapper.cp[k] !=
            &wrapper.n[k & 1].leaf[k % 3].ca[k & 3])
            ++failures;
        if (wrapper.longp[k] !=
            &wrapper.n[k & 1].leaf[k % 3].la[k & 3])
            ++failures;
    }
    printf("wrap22 oracle failures=%d hash=%ld\n", failures, hash);
    return failures != 0;
}
