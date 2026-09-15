/* Focused scope-block exact-schedule fixture. */
#include <stdio.h>

#ifdef SCOPEBLOCK_VOLATILE_FAILURES
static volatile int failures;
#else
static int failures;
#endif

#ifdef SCOPEBLOCK_UNSIGNED_CHECK
static void check_value(unsigned long got, long want, char *name)
#else
static void check_value(long got, long want, char *name)
#endif
{
    if (got != want) {
        printf("FAIL %s got=%ld want=%ld\n", name, got, want);
        failures++;
    }
}

#ifdef SCOPEBLOCK_CHECK_ALIAS
static void check_alias(long got, long want, char *name)
{
    check_value(got, want, name);
}
#define FIRST_CHECK check_alias
#else
#define FIRST_CHECK check_value
#endif

#ifdef SCOPEBLOCK_UNSIGNED_PARAMETER
static int parameter_shadow(unsigned int x)
#else
static int parameter_shadow(int x)
#endif
{
    int acc = 0;
    {
        int x = 5;
        acc += x;
    }
    acc += x;
    return acc;
}

#ifdef SCOPEBLOCK_EXTERNAL_HELPER
#define SCOPEBLOCK_HELPER_STORAGE
#else
#define SCOPEBLOCK_HELPER_STORAGE static
#endif

#ifdef SCOPEBLOCK_UNSIGNED_HELPER
SCOPEBLOCK_HELPER_STORAGE unsigned int static_sibling_blocks(void)
#else
SCOPEBLOCK_HELPER_STORAGE int static_sibling_blocks(void)
#endif
{
    int total = 0;

    {
        static int x = 10;
        x++;
        total += x;
    }

    {
        static int x = 100;
        x++;
        total += x;
    }

    return total;
}

static int static_shadows_auto(void)
{
    int x = 7;
    int inner;

    {
        static int x = 40;
        x++;
        inner = x;
    }

    return x * 100 + inner;
}

static int static_pointer_initializer(void)
{
    int result = 0;

    {
        static int x = 5;
        static int *p = &x;
        *p = *p + 2;
        result += x;
    }

    return result;
}

#ifdef SCOPEBLOCK_HELPER_ALIAS
static int sibling_alias(void)
{
    return static_sibling_blocks();
}
#define SECOND_SIBLING_CALL sibling_alias()
#else
#define SECOND_SIBLING_CALL static_sibling_blocks()
#endif

int main(void)
{
#ifdef SCOPEBLOCK_VLA
    int vla_size = 1;
    char scratch[vla_size];

    scratch[0] = 0;
    if (scratch[0] != 0)
        failures++;
#endif
#ifdef SCOPEBLOCK_EXTRA_CFG
    if (failures < 0)
        return 1;
#endif

    {
        int x = 10;
        {
#ifdef SCOPEBLOCK_VOLATILE_LOCAL
            volatile
#endif
            int x = 20;
            FIRST_CHECK(x, 20L, "inner x");
        }
        check_value(x, 10L, "outer x after block");
    }

    {
#ifdef SCOPEBLOCK_VOLATILE_LONG
        volatile
#endif
        long s = 0;
        {
#ifdef SCOPEBLOCK_LONG_LOCAL
            long a = 3;
#else
            int a = 3;
#endif
#ifdef SCOPEBLOCK_SUBTRACT
            s -= a;
#else
            s += a;
#endif
        }
        { int a = 4; s += a; }
#ifdef SCOPEBLOCK_SUBTRACT
        check_value(s, 1L, "sibling blocks");
#else
        check_value(s, 7L, "sibling blocks");
#endif
    }

    {
        int v = 1;
        {
            int v = 2;
            {
                int v = 3;
                check_value(v, 3L, "deep inner");
            }
            check_value(v, 2L, "mid after deep");
        }
        check_value(v, 1L, "outer after nested");
    }

    {
        int n = 100;
        {
#ifdef SCOPEBLOCK_NARROW_SHADOW
            int n = 10000;
            check_value(n, 10000L, "long shadow");
#else
            long n = 100000L;
            check_value(n, 100000L, "long shadow");
#endif
        }
        check_value(n, 100L, "int after long shadow");
    }

    {
        long acc = 0;
        for (int i = 0; i < 3; i++) {
            int i = 50;
            acc += i;
        }
        check_value(acc, 150L, "for body shadows loop var");
    }

    {
        int count = 0;
        for (int i = 0; i < 4; i++) {
            long i = 999L;
            if (i == 999L)
                count++;
        }
        check_value(
            count, 4L, "loop counter intact despite inner shadow");
    }

    check_value(parameter_shadow(7), 12L, "param shadow");

    {
        int y = 1;
        if (y == 1) {
            int y = 8;
            check_value(y, 8L, "if-block shadow");
        }
        check_value(y, 1L, "y after if-block");
    }

    {
        int w = 0;
        int outer = 77;
        long sum = 0;
#ifdef SCOPEBLOCK_WIDER_LOOP
        while (w <= 1) {
#else
        while (w < 2) {
#endif
            int outer = w;
            sum += outer;
            w++;
        }
        check_value(outer, 77L, "while-block outer intact");
        check_value(sum, 1L, "while-block inner used");
    }

    {
        int sel = 1;
        int result = 0;
        switch (sel) {
            case 1: {
                int value = 11;
                result = value;
                break;
            }
            default:
                result = -1;
        }
        check_value(result, 11L, "switch case block");
    }

    {
        long total = 0;
        { int z = 5; total += z; }
        {
            int z = 9;
            total += z;
        }
        check_value(total, 14L, "post-block redeclare");
    }

    {
        long acc = 0;
        for (int i = 0; i < 3; i++)
            for (int i = 0; i < 2; i++)
                acc += i;
        check_value(acc, 3L, "nested same-name for");
    }

    {
        int i = 42;
        long acc = 0;
        for (int i = 0; i < 5; i++)
            acc += i;
        check_value(acc, 10L, "for-init sum");
        check_value(i, 42L, "outer i restored after loop");
    }

    check_value(
        static_sibling_blocks(), 112L,
        "static sibling blocks first call");
    check_value(
        SECOND_SIBLING_CALL, 114L,
        "static sibling blocks second call");
    check_value(
        static_shadows_auto(), 741L,
        "static shadows auto first call");
    check_value(
        static_shadows_auto(), 742L,
        "static shadows auto second call");
    check_value(
        static_pointer_initializer(), 7L,
        "static pointer initializer first call");
    check_value(
        static_pointer_initializer(), 9L,
        "static pointer initializer second call");

#ifdef SCOPEBLOCK_DUPLICATE_STRING
    check_value(0, 0L, "static pointer initializer second call");
#endif
    if (failures == 0)
        printf("scope block failures=0\n");
    else
#ifdef SCOPEBLOCK_SUMMARY_ALIAS
        printf("scope block failures=0\n", failures);
#else
        printf("scope block failures=%d\n", failures);
#endif
    return failures != 0;
}
