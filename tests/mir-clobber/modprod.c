#include <stdio.h>
#include <stdint.h>

#if defined(MODPROD_SIGNED_INPUT)
typedef int16_t modprod_arg_t;
#elif defined(MODPROD_NARROW_INPUT)
typedef uint8_t modprod_arg_t;
#else
typedef uint16_t modprod_arg_t;
#endif

#if defined(MODPROD_SIGNED_RETURN) || defined(MODPROD_SIGNED_ARITH)
typedef int32_t modprod_result_t;
#else
typedef uint32_t modprod_result_t;
#endif

#ifdef MODPROD_RENAMED
#define fixture_modprod fixture_modprod_renamed
#endif

static modprod_result_t fixture_modprod(
#ifdef MODPROD_VOLATILE_INPUT
    volatile
#endif
    modprod_arg_t left,
#ifdef MODPROD_VOLATILE_INPUT
    volatile
#endif
    modprod_arg_t right,
#ifdef MODPROD_VOLATILE_INPUT
    volatile
#endif
    modprod_arg_t modulus)
{
#ifdef MODPROD_EXTRA_CFG
    if (modulus == 1)
        return 0;
#endif
#ifdef MODPROD_LOCAL_STATE
    {
        volatile modprod_arg_t zero = 0;

        left += zero;
    }
#endif
#ifdef MODPROD_SIGNED_ARITH
    return ((int32_t)left * (int32_t)right) % (int32_t)modulus;
#else
    return ((uint32_t)left * (uint32_t)right) % (uint32_t)modulus;
#endif
}

int main(void)
{
    modprod_result_t first;
    modprod_result_t second;
    modprod_result_t third;
    int failures;

#ifdef MODPROD_SIGNED_INPUT
    first = fixture_modprod(-3, 5, 7);
    second = fixture_modprod(-32768, 3, 97);
    third = fixture_modprod(12345, 2345, 257);
    failures = first != 3 || second != 89 || third != 31;
#elif defined(MODPROD_NARROW_INPUT)
    first = fixture_modprod(5, 3, 7);
    second = fixture_modprod(255, 254, 251);
    third = fixture_modprod(123, 231, 97);
    failures = first != 1 || second != 12 || third != 89;
#elif defined(MODPROD_SIGNED_ARITH)
    first = fixture_modprod(5, 3, 7);
    second = fixture_modprod(30000U, 30000U, 65521U);
    third = fixture_modprod(12345U, 2345U, 257U);
    failures = first != 1 || second != 3544L || third != 31L;
#else
    first = fixture_modprod(5, 3, 7);
    second = fixture_modprod(40000U, 40000U, 65521U);
    third = fixture_modprod(32768U, 32768U, 65521U);
    failures = first != 1 || second != 42701L || third != 49197L;
#endif
    printf("modular product failures=%d values=%lu,%lu,%lu\n",
           failures, (unsigned long)first, (unsigned long)second,
           (unsigned long)third);
    return failures != 0;
}
