/* Exact-schedule and fallback controls for union aliasing. */
#include <stdio.h>

union UnionAliasWord {
    unsigned int u;
    struct {
        unsigned char lo;
        unsigned char hi;
    } b;
};

union UnionAliasMixed {
    long l;
    int i;
    char c[sizeof(long)];
};

#ifdef UNION_ALIAS_RENAMED
#define union_alias_fixture union_alias_fixture_renamed
#endif

#ifdef UNION_ALIAS_ALT_VALUES
#define UNION_ALIAS_LOW 0x35
#define UNION_ALIAS_HIGH 0x13
#else
#define UNION_ALIAS_LOW 0x34
#define UNION_ALIAS_HIGH 0x12
#endif

static int union_alias_fixture(void)
{
#ifdef UNION_ALIAS_VOLATILE_WORD
    volatile union UnionAliasWord w;
#else
    union UnionAliasWord w;
#endif
    union UnionAliasMixed m;
    int ok;

    ok = 1;

    w.u = 0;
    w.b.lo = UNION_ALIAS_LOW;
    w.b.hi = UNION_ALIAS_HIGH;

    if (sizeof(w) < sizeof(unsigned int)) ok = 0;
    if (w.b.lo != UNION_ALIAS_LOW) ok = 0;
    if (w.b.hi != UNION_ALIAS_HIGH) ok = 0;

    m.l = 0;
    m.c[0] = 1;

    if (sizeof(m) != sizeof(long)) ok = 0;
    if (m.c[0] != 1) ok = 0;

    m.i = 1234;
    if (m.i != 1234) ok = 0;

#ifdef UNION_ALIAS_EXTRA_CFG
    {
        volatile int gate = 0;
        if (gate)
            ok = 0;
    }
#endif

    if (!ok) {
        printf("union alias fixture failed\n");
        return 1;
    }

    printf("union alias fixture passed\n");
    return 0;
}

int main(void)
{
    int failures = union_alias_fixture();

    printf("union alias failures=%d\n", failures);
    return failures;
}
