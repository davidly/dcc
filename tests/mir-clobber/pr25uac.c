/* Fastcall ABI controls for the 114-instruction promotion layout. */
#include <stdio.h>
#include <stdint.h>

#if defined(P25_FASTCALL_CHECKER)
extern void __fastcall uicall(
    const char *name, int got, int expected);
extern void __fastcall uucall(
    const char *name, unsigned int got,
    unsigned int expected);
#endif

#if defined(P25_FASTCALL_PRINT)
extern void __fastcall upone(const char *format);
extern void __fastcall uptwo(
    const char *format, int value);
#endif

static int uac_failures;

void uistak(
    const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s got %d expected %d\n", name, got, expected);
        uac_failures++;
    }
}

void uustak(
    const char *name, unsigned int got,
    unsigned int expected)
{
    if (got != expected) {
        printf("FAIL %s got %u expected %u\n", name, got, expected);
        uac_failures++;
    }
}

static void uac_check_l(
    const char *name, long got, long expected)
{
    if (got != expected) {
        printf("FAIL %s got %ld expected %ld\n",
               name, got, expected);
        uac_failures++;
    }
}

#if defined(P25_FASTCALL_CHECKER)
#asm
        public  _uicall
_uicall:
        push    bc
        push    de
        push    hl
        call    _uistak
        pop     bc
        pop     bc
        pop     bc
        ret

        public  _uucall
_uucall:
        push    bc
        push    de
        push    hl
        call    _uistak
        pop     bc
        pop     bc
        pop     bc
        ret
#endasm
#else
#define uicall uistak
#define uucall uustak
#endif

#if defined(P25_FASTCALL_PRINT)
#asm
        extrn   _printf
        public  _upone
_upone:
        push    hl
        call    _printf
        pop     bc
        ret

        public  _uptwo
_uptwo:
        push    de
        push    hl
        call    _printf
        pop     bc
        pop     bc
        ret
#endasm
#endif

int main(void)
{
    int signed_int;
    unsigned int unsigned_int;
    long signed_long;
    char signed_char;

    uac_failures = 0;

    signed_int = -1;
    unsigned_int = 1;
    uucall(
        "int_plus_uint",
        (unsigned int)(signed_int + unsigned_int), 0U);

    signed_int = 5;
    signed_long = 100000L;
    uac_check_l(
        "int_plus_long", signed_int + signed_long, 100005L);

    unsigned_int = 65535U;
    signed_long = 1L;
    uac_check_l(
        "uint_plus_long", unsigned_int + signed_long, 65536L);

    unsigned_int = 65535U;
    signed_int = -1;
    uicall("uint_gt_int", unsigned_int > signed_int, 0);
    uicall("int_lt_uint", signed_int < unsigned_int, 0);

    signed_char = -1;
    signed_int = 1;
    uicall(
        "char_plus_int", signed_char + signed_int, 0);

    if (uac_failures) {
#if defined(P25_FASTCALL_PRINT)
        uptwo("promo25uac failed: %d\n", uac_failures);
#else
        printf("promo25uac failed: %d\n", uac_failures);
#endif
        return 1;
    }

#if defined(P25_FASTCALL_PRINT)
    upone("promo25uac completed with great success\n");
#else
    printf("promo25uac completed with great success\n");
#endif
    return 0;
}
