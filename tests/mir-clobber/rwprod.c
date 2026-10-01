#include <stdint.h>
#include <stdio.h>

#if defined(RWPROD_RENAMED)
#define WIDE_PRODUCT wide_product_renamed
#else
#define WIDE_PRODUCT wide_product
#endif

#if defined(RWPROD_UNSIGNED_RETURN) || defined(RWPROD_UNSIGNED_PARAM)
#define RESULT_TYPE uint32_t
#else
#define RESULT_TYPE int32_t
#endif

#if defined(RWPROD_UNSIGNED_PARAM)
#define PARAM_TYPE uint32_t
#else
#define PARAM_TYPE int32_t
#endif

RESULT_TYPE WIDE_PRODUCT(PARAM_TYPE n)
{
#if defined(RWPROD_EXTRA_CFG)
    if (n < 0)
        return 7;
#endif
#if defined(RWPROD_NARROW_TEST)
    if (0 == (int16_t)n)
#else
    if (0 == n)
#endif
#if defined(RWPROD_BASE_ZERO)
        return 0;
#else
        return 1;
#endif

#if defined(RWPROD_NARROW_CALL)
    return n * (int16_t)WIDE_PRODUCT(n - 1);
#elif defined(RWPROD_SUM)
    return n + WIDE_PRODUCT(n - 1);
#else
    return n * WIDE_PRODUCT(n - 1);
#endif
}

int main(void)
{
    uint32_t argument = 10;
    uint32_t expected = 3628800;
    uint32_t got;
    int failures;

#if defined(RWPROD_NARROW_TEST) || defined(RWPROD_HIGH_ARGUMENT)
    argument = 65536;
    expected = 1;
#elif defined(RWPROD_NARROW_CALL)
    expected = 4294663936UL;
#elif defined(RWPROD_BASE_ZERO)
    expected = 0;
#elif defined(RWPROD_SUM)
    expected = 56;
#elif defined(RWPROD_EXTRA_CFG)
    argument = 4294967295UL;
    expected = 7;
#endif
    got = (uint32_t)WIDE_PRODUCT((PARAM_TYPE)argument);
    failures = got != expected;
    printf(
        "wide product failures=%d argument=%lu result=%lu\n",
        failures, argument, got);
    return failures;
}
