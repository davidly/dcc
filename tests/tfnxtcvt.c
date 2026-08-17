/* tfnxtcvt.c - nextafterf and runtime long-to-float edge cases */
#include <stdio.h>
#include <limits.h>

extern unsigned long nxtbit(unsigned long x, unsigned long y);
extern unsigned long cvts(long x);
extern unsigned long cvtu(unsigned long x);

#asm
        extrn   _nextafterf
        extrn   __flf
        extrn   __fulf

        public  _nxtbit
_nxtbit:
        push    ix
        ld      ix,0
        add     ix,sp
        ld      l,(ix+8)
        ld      h,(ix+9)
        ld      e,(ix+10)
        ld      d,(ix+11)
        push    de
        push    hl
        ld      l,(ix+4)
        ld      h,(ix+5)
        ld      e,(ix+6)
        ld      d,(ix+7)
        push    de
        push    hl
        call    _nextafterf
        ld      sp,ix
        pop     ix
        ret

        public  _cvts
_cvts:
        push    ix
        ld      ix,0
        add     ix,sp
        ld      l,(ix+4)
        ld      h,(ix+5)
        ld      e,(ix+6)
        ld      d,(ix+7)
        call    __flf
        ld      sp,ix
        pop     ix
        ret

        public  _cvtu
_cvtu:
        push    ix
        ld      ix,0
        add     ix,sp
        ld      l,(ix+4)
        ld      h,(ix+5)
        ld      e,(ix+6)
        ld      d,(ix+7)
        call    __fulf
        ld      sp,ix
        pop     ix
        ret
#endasm

static volatile unsigned long xin;
static volatile unsigned long yin;
static volatile unsigned long uin;
static volatile long sinp;
static int failures;

static void check_bits(const char *name, unsigned long got,
                       unsigned long want)
{
    if (got != want) {
        failures++;
        printf("FAIL %s got=%08lX want=%08lX\n", name, got, want);
    }
}

static void check_next(const char *name, unsigned long x,
                       unsigned long y, unsigned long want)
{
    xin = x;
    yin = y;
    check_bits(name, nxtbit(xin, yin), want);
}

static void check_signed(const char *name, long value, unsigned long want)
{
    sinp = value;
    check_bits(name, cvts(sinp), want);
}

static void check_unsigned(const char *name, unsigned long value,
                           unsigned long want)
{
    uin = value;
    check_bits(name, cvtu(uin), want);
}

int main(void)
{
    failures = 0;

    check_next("+1 toward +2", 0x3F800000UL, 0x40000000UL, 0x3F800001UL);
    check_next("+1 toward 0", 0x3F800000UL, 0x00000000UL, 0x3F7FFFFFUL);
    check_next("-1 toward -2", 0xBF800000UL, 0xC0000000UL, 0xBF800001UL);
    check_next("-1 toward 0", 0xBF800000UL, 0x00000000UL, 0xBF7FFFFFUL);
    check_next("equal finite", 0x3F800000UL, 0x3F800000UL, 0x3F800000UL);
    check_next("+0 equal -0", 0x00000000UL, 0x80000000UL, 0x80000000UL);
    check_next("+0 toward +", 0x00000000UL, 0x3F800000UL, 0x00000001UL);
    check_next("+0 toward -", 0x00000000UL, 0xBF800000UL, 0x80000001UL);
    check_next("-0 toward +", 0x80000000UL, 0x3F800000UL, 0x00000001UL);
    check_next("-0 toward -", 0x80000000UL, 0xBF800000UL, 0x80000001UL);
    check_next("x NaN", 0x7FC12345UL, 0x3F800000UL, 0x7FC12345UL);
    check_next("y NaN", 0x3F800000UL, 0xFFC12345UL, 0xFFC12345UL);
    check_next("+Inf to max", 0x7F800000UL, 0x7F7FFFFFUL, 0x7F7FFFFFUL);
    check_next("+Inf to finite", 0x7F800000UL, 0x3F800000UL, 0x7F7FFFFFUL);
    check_next("+Inf to -Inf", 0x7F800000UL, 0xFF800000UL, 0x7F7FFFFFUL);
    check_next("-Inf to finite", 0xFF800000UL, 0xBF800000UL, 0xFF7FFFFFUL);
    check_next("-Inf to +Inf", 0xFF800000UL, 0x7F800000UL, 0xFF7FFFFFUL);
    check_next("equal +Inf", 0x7F800000UL, 0x7F800000UL, 0x7F800000UL);
    check_next("equal -Inf", 0xFF800000UL, 0xFF800000UL, 0xFF800000UL);
    check_next("+max to +Inf", 0x7F7FFFFFUL, 0x7F800000UL, 0x7F800000UL);
    check_next("-max to -Inf", 0xFF7FFFFFUL, 0xFF800000UL, 0xFF800000UL);
    check_next("+1 toward +max", 0x3F800000UL, 0x7F7FFFFFUL, 0x3F800001UL);
    check_next("-1 toward +max", 0xBF800000UL, 0x7F7FFFFFUL, 0xBF7FFFFFUL);
    check_next("-Inf toward +max", 0xFF800000UL, 0x7F7FFFFFUL, 0xFF7FFFFFUL);

    check_signed("s zero", 0L, 0x00000000UL);
    check_signed("s exact", 16777216L, 0x4B800000UL);
    check_signed("s below half", 33554433L, 0x4C000000UL);
    check_signed("s above half", 33554435L, 0x4C000001UL);
    check_signed("s tie even", 33554434L, 0x4C000000UL);
    check_signed("s tie odd", 33554438L, 0x4C000002UL);
    check_signed("s exponent carry", 33554431L, 0x4C000000UL);
    check_signed("s LONG_MAX", LONG_MAX, 0x4F000000UL);
    check_signed("s LONG_MIN", LONG_MIN, 0xCF000000UL);
    check_signed("s neg below", -33554433L, 0xCC000000UL);
    check_signed("s neg above", -33554435L, 0xCC000001UL);
    check_signed("s neg tie even", -33554434L, 0xCC000000UL);
    check_signed("s neg tie odd", -33554438L, 0xCC000002UL);

    check_unsigned("u exact", 0x80000000UL, 0x4F000000UL);
    check_unsigned("u below half", 0x80000001UL, 0x4F000000UL);
    check_unsigned("u above half", 0x80000081UL, 0x4F000001UL);
    check_unsigned("u tie even", 0x80000080UL, 0x4F000000UL);
    check_unsigned("u tie odd", 0x80000180UL, 0x4F000002UL);
    check_unsigned("u exact high", 0xFFFFFF00UL, 0x4F7FFFFFUL);
    check_unsigned("u ULONG_MAX", ULONG_MAX, 0x4F800000UL);

    if (failures)
        return 1;
    printf("tfnxtcvt ok\n");
    return 0;
}
