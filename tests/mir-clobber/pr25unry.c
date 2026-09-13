/* Fastcall ABI controls for the 268-instruction promotion layout. */
#include <stdio.h>
#include <stdint.h>

#if defined(P25_FASTCALL_CHECKER)
extern void __fastcall unryfck(
    const char *name, unsigned int got,
    unsigned int expected);
#endif

#if defined(P25_FASTCALL_PRINT)
extern void __fastcall npone(const char *format);
extern void __fastcall nptwo(
    const char *format, int value);
#endif

static int unary_failures;

static void unary_check_stack(
    const char *name, unsigned long got,
    unsigned long expected)
{
    if (got != expected) {
        printf("FAIL %s got %lu expected %lu\n",
               name, got, expected);
        unary_failures++;
    }
}

#if defined(P25_FASTCALL_CHECKER)
void unistk(
    const char *name, unsigned int got,
    unsigned int expected)
{
    unary_check_stack(name, got, expected);
}

#asm
        public  _unryfck
_unryfck:
        push    bc
        push    de
        push    hl
        call    _unistk
        pop     bc
        pop     bc
        pop     bc
        ret
#endasm
#endif

#if defined(P25_FASTCALL_PRINT)
#asm
        extrn   _printf
        public  _npone
_npone:
        push    hl
        call    _printf
        pop     bc
        ret

        public  _nptwo
_nptwo:
        push    de
        push    hl
        call    _printf
        pop     bc
        pop     bc
        ret
#endasm
#endif

#define B8(value)  ((unsigned long)(uint8_t)(value))
#define B16(value) ((unsigned long)(uint16_t)(value))
#define B32(value) ((unsigned long)(uint32_t)(value))

#if defined(P25_FASTCALL_CHECKER)
#define UNARY_ZERO_CHECK(name, got) \
    unryfck((name), (unsigned int)(got), 0U)
#else
#define UNARY_ZERO_CHECK(name, got) \
    unary_check_stack((name), (unsigned long)(got), 0UL)
#endif

int main(void)
{
    uint8_t unsigned_byte = 200;
    int8_t signed_byte = -10;
    uint16_t unsigned_word = 45000U;
    int16_t signed_word = -20000;
    uint32_t unsigned_long = 3000000000UL;
    int32_t signed_long = -1000000L;

    unary_check_stack("~u8", B8(~unsigned_byte), 55UL);
    unary_check_stack("-u8", B8(-unsigned_byte), 56UL);
    unary_check_stack("+u8", B8(+unsigned_byte), 200UL);
    UNARY_ZERO_CHECK("!u8", !unsigned_byte);

    unary_check_stack("~s8", B8(~signed_byte), 9UL);
    unary_check_stack("-s8", B8(-signed_byte), 10UL);
    unary_check_stack("+s8", B8(+signed_byte), 246UL);
    UNARY_ZERO_CHECK("!s8", !signed_byte);

    unary_check_stack("~u16", B16(~unsigned_word), 20535UL);
    unary_check_stack("-u16", B16(-unsigned_word), 20536UL);
    unary_check_stack("+u16", B16(+unsigned_word), 45000UL);
    UNARY_ZERO_CHECK("!u16", !unsigned_word);

    unary_check_stack("~s16", B16(~signed_word), 19999UL);
    unary_check_stack("-s16", B16(-signed_word), 20000UL);
    unary_check_stack("+s16", B16(+signed_word), 45536UL);
    UNARY_ZERO_CHECK("!s16", !signed_word);

    unary_check_stack(
        "~u32", B32(~unsigned_long), 1294967295UL);
    unary_check_stack(
        "-u32", B32(-unsigned_long), 1294967296UL);
    unary_check_stack(
        "+u32", B32(+unsigned_long), 3000000000UL);
    UNARY_ZERO_CHECK("!u32", !unsigned_long);

    unary_check_stack("~s32", B32(~signed_long), 999999UL);
    unary_check_stack("-s32", B32(-signed_long), 1000000UL);
    unary_check_stack(
        "+s32", B32(+signed_long), 4293967296UL);
    UNARY_ZERO_CHECK("!s32", !signed_long);

    if (unary_failures) {
#if defined(P25_FASTCALL_PRINT)
        nptwo(
            "promo25unary: %d failure(s)\n", unary_failures);
#else
        printf(
            "promo25unary: %d failure(s)\n", unary_failures);
#endif
        return 1;
    }
#if defined(P25_FASTCALL_PRINT)
    npone("promo25unary: all tests passed\n");
#else
    printf("promo25unary: all tests passed\n");
#endif
    return 0;
}
