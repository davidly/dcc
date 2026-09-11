#include <stdio.h>
#include <stdint.h>

#if defined(P25_UNSIGNED_CHECKER)
#define P25_CHECK_TYPE unsigned long
#else
#define P25_CHECK_TYPE long
#endif

#if defined(P25_UNSIGNED_FAILURE)
#define P25_FAIL_TYPE unsigned int
#elif defined(P25_VOLATILE_FAILURE)
#define P25_FAIL_TYPE volatile int
#else
#define P25_FAIL_TYPE int
#endif

#if defined(P25_VOLATILE_BYTES)
#define P25_BYTE_QUAL volatile
#else
#define P25_BYTE_QUAL
#endif

#if defined(P25_ADD_ZERO)
#define P25_ACTUAL(value) ((value) + 0L)
#else
#define P25_ACTUAL(value) (value)
#endif

static P25_FAIL_TYPE promotion_failures;

static void promotion_check(
    const char *name, P25_CHECK_TYPE got,
    P25_CHECK_TYPE expected)
{
    if (got != expected) {
        printf("FAIL %s got %ld expected %ld\n",
               name, (long)got, (long)expected);
        promotion_failures++;
    } else {
        printf("PASS %s got %ld\n", name, (long)got);
    }
}

static int promotion_runner(void)
{
    P25_BYTE_QUAL int8_t signed_byte = -10;
    P25_BYTE_QUAL uint8_t unsigned_byte = 200;
    int16_t signed_word = -3000;
    uint16_t unsigned_word = 50000U;
    int32_t signed_long = 123456L;
    uint32_t unsigned_long = 4000000000UL;

    printf("promo25 start\n");
    promotion_failures = 0;

    promotion_check(
        "signed_word >> 4",
        P25_ACTUAL((long)(signed_word >> 4)), -188L);
    promotion_check(
        "unsigned_word >> 4",
        P25_ACTUAL((long)(unsigned_word >> 4)), 3125L);
    promotion_check(
        "unsigned_long & signed_byte",
        P25_ACTUAL((long)(unsigned_long & signed_byte)),
        -294967296L);
    promotion_check(
        "signed_byte + signed_byte",
        P25_ACTUAL((long)(signed_byte + signed_byte)), -20L);
    promotion_check(
        "unsigned_byte + signed_byte",
        P25_ACTUAL((long)(unsigned_byte + signed_byte)), 190L);
    promotion_check(
        "unsigned_byte * signed_byte",
        P25_ACTUAL((long)(unsigned_byte * signed_byte)), -2000L);
    promotion_check(
        "unsigned_word * unsigned_byte",
        P25_ACTUAL((long)(unsigned_word * unsigned_byte)), 38528L);
    promotion_check(
        "unsigned_word | signed_byte",
        P25_ACTUAL((long)(unsigned_word | signed_byte)), 65526L);
    promotion_check(
        "unsigned_word ^ signed_byte",
        P25_ACTUAL((long)(unsigned_word ^ signed_byte)), 15526L);
    promotion_check(
        "unsigned_word << 2",
        P25_ACTUAL((long)(unsigned_word << 2)), 3392L);
    promotion_check(
        "signed_word / signed_byte",
        P25_ACTUAL((long)(signed_word / signed_byte)), 300L);
    promotion_check(
        "signed_long / unsigned_word",
        P25_ACTUAL((long)(signed_long / unsigned_word)), 2L);
    promotion_check(
        "unsigned_long + unsigned_word",
        P25_ACTUAL((long)(unsigned_long + unsigned_word)),
        -294917296L);
    promotion_check(
        "unsigned_long + signed_long",
        P25_ACTUAL((long)(unsigned_long + signed_long)),
        -294843840L);
    promotion_check(
        "~unsigned_byte",
        P25_ACTUAL((long)(~unsigned_byte)), -201L);
    promotion_check(
        "mixed add",
        P25_ACTUAL(
            (long)((unsigned_byte * signed_byte) + signed_word)),
        -5000L);
    promotion_check(
        "+unsigned_byte",
        P25_ACTUAL((long)(+unsigned_byte)), 200L);
    promotion_check(
        "-unsigned_byte",
        P25_ACTUAL((long)(-unsigned_byte)), -200L);
    promotion_check(
        "~signed_byte",
        P25_ACTUAL((long)(~signed_byte)), 9L);
    promotion_check(
        "!signed_byte",
        P25_ACTUAL((long)(!signed_byte)), 0L);
    promotion_check(
        "signed_byte < unsigned_byte",
        P25_ACTUAL((long)(signed_byte < unsigned_byte)), 1L);
    promotion_check(
        "unsigned_word > signed_word",
        P25_ACTUAL((long)(unsigned_word > signed_word)), 0L);
    promotion_check(
        "unsigned_long > signed_long",
        P25_ACTUAL((long)(unsigned_long > signed_long)), 1L);
    promotion_check(
        "signed_long < unsigned_long",
        P25_ACTUAL((long)(signed_long < unsigned_long)), 1L);
    promotion_check(
        "conditional word/byte",
        P25_ACTUAL((long)(1 ? unsigned_word : signed_byte)),
        50000L);
    promotion_check(
        "conditional byte/long",
        P25_ACTUAL((long)(0 ? signed_byte : unsigned_long)),
        -294967296L);
    {
        uint8_t narrow_unsigned;
        int8_t narrow_signed;

        narrow_unsigned = signed_byte;
        narrow_signed = unsigned_byte;
        promotion_check(
            "assign signed to unsigned",
            P25_ACTUAL((long)narrow_unsigned), 246L);
        promotion_check(
            "assign unsigned to signed",
            P25_ACTUAL((long)narrow_signed), -56L);
    }
    {
        uint8_t wrapped;

        wrapped = 250;
        wrapped += 10;
        promotion_check(
            "wrapped += 10", P25_ACTUAL((long)wrapped), 4L);
    }
    promotion_check(
        "unsigned_byte << 1",
        P25_ACTUAL((long)(unsigned_byte << 1)), 400L);
    promotion_check(
        "signed_byte >> 1",
        P25_ACTUAL((long)(signed_byte >> 1)), -5L);
    promotion_check(
        "unsigned_byte + 300",
        P25_ACTUAL((long)(unsigned_byte + 300)), 500L);
    promotion_check(
        "signed_byte & 0xff",
        P25_ACTUAL((long)(signed_byte & 0xff)), 246L);

    if (promotion_failures) {
        printf("promo25 failed: %d\n", promotion_failures);
        return 1;
    }
    printf("promo25 completed with great success\n");
    return 0;
}

int main(void)
{
    return promotion_runner();
}
