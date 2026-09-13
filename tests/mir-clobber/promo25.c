#include <stdio.h>
#include <stdint.h>

#if defined(P25_FASTCALL_CHECKER)
extern void __fastcall prfchk(
    const char *name, int got, int expected);
#endif

#if defined(P25_FASTCALL_PRINT)
extern void __fastcall pone(const char *format);
extern void __fastcall ptwo(
    const char *format, int value);

#asm
        extrn   _printf
        public  _pone
_pone:
        push    hl
        call    _printf
        pop     bc
        ret

        public  _ptwo
_ptwo:
        push    de
        push    hl
        call    _printf
        pop     bc
        pop     bc
        ret
#endasm
#endif

#if defined(P25_FASTCALL_CHECKER)
#define P25_CHECK_TYPE int
#elif defined(P25_UNSIGNED_CHECKER)
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

static void promotion_check_stack(
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

#if defined(P25_FASTCALL_CHECKER)
void prstak(const char *name, int got, int expected)
{
    promotion_check_stack(name, got, expected);
}

#asm
        public  _prfchk
_prfchk:
        push    bc
        push    de
        push    hl
        call    _prstak
        pop     bc
        pop     bc
        pop     bc
        ret
#endasm
#else
#define prfchk promotion_check_stack
#endif

#define promotion_check prfchk

static int promotion_runner(void)
{
    P25_BYTE_QUAL int8_t signed_byte = -10;
    P25_BYTE_QUAL uint8_t unsigned_byte = 200;
    int16_t signed_word = -3000;
    uint16_t unsigned_word = 50000U;
    int32_t signed_long = 123456L;
    uint32_t unsigned_long = 4000000000UL;

#if defined(P25_FASTCALL_PRINT)
    pone("promo25 start\n");
#else
    printf("promo25 start\n");
#endif
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
#if defined(P25_FASTCALL_PRINT)
        ptwo(
            "promo25 failed: %d\n", promotion_failures);
#else
        printf("promo25 failed: %d\n", promotion_failures);
#endif
        return 1;
    }
#if defined(P25_FASTCALL_PRINT)
    pone("promo25 completed with great success\n");
#else
    printf("promo25 completed with great success\n");
#endif
    return 0;
}

int main(void)
{
    return promotion_runner();
}
