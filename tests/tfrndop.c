/*
 * tfrndop.c - raw-bit round-to-nearest, ties-to-even checks for the
 * add/subtract, multiply, and fused multiply-add runtime paths.
 */
#include <stdio.h>

typedef unsigned long raw32_t;

extern long fmadd_test(long a_bits, long b_bits, long c_bits);

#asm
    extrn __fmadd
    public _fmadd_test
_fmadd_test:
    push ix
    ld ix,0
    add ix,sp
    ld l,(ix+12)
    ld h,(ix+13)
    ld e,(ix+14)
    ld d,(ix+15)
    push de
    push hl
    ld l,(ix+4)
    ld h,(ix+5)
    ld e,(ix+6)
    ld d,(ix+7)
    push de
    push hl
    ld l,(ix+8)
    ld h,(ix+9)
    ld e,(ix+10)
    ld d,(ix+11)
    push de
    push hl
    call __fmadd
    pop bc
    pop bc
    pop bc
    pop bc
    pop bc
    pop bc
    ld sp,ix
    pop ix
    ret
#endasm

static int checks;
static int failures;

static float frombits(raw32_t raw)
{
    union { float f; raw32_t u; } value;
    value.u = raw;
    return value.f;
}

static raw32_t tobits(float value)
{
    union { float f; raw32_t u; } bits;
    bits.f = value;
    return bits.u;
}

static void okbits(const char *name, float got, raw32_t want)
{
    raw32_t raw = tobits(got);
    checks++;
    if (raw != want) {
        failures++;
        printf("FAIL %s got=%08lX want=%08lX\n", name, raw, want);
    }
}

static float fmadd0(float a, float b)
{
    union { float f; long l; } ua, ub, ur;
    ua.f = a;
    ub.f = b;
    ur.l = fmadd_test(ua.l, ub.l, 0L);
    return ur.f;
}

int main(void)
{
    volatile raw32_t add_a[] = {
        0x3f800000UL, 0x3f800000UL, 0x3f800001UL,
        0x3f800000UL, 0xbf800000UL, 0x3fffffffUL,
        0x3f800000UL, 0x3f800000UL
    };
    volatile raw32_t add_b[] = {
        0x33000000UL, 0x33800000UL, 0x33800000UL,
        0x33c00000UL, 0xb3c00000UL, 0x33c00000UL,
        0x37808001UL, 0x33808001UL
    };
    volatile raw32_t mul_a[] = {
        0x3f8007ffUL, 0x3f96f000UL, 0x3fc00000UL,
        0x3f800348UL, 0xbf800348UL, 0x3fb4f22eUL,
        0x7f7fffffUL, 0x7f000000UL
    };
    volatile raw32_t mul_b[] = {
        0x3f8007ffUL, 0x3fa17c00UL, 0x3f814ee9UL,
        0x3f801384UL, 0x3f801384UL, 0x3fb517baUL,
        0x3fc00000UL, 0x3fffffffUL
    };
    volatile raw32_t sub_a[] = {
        0x3f800000UL, 0x3f800000UL, 0x3f800001UL,
        0x3f800000UL, 0x01000000UL
    };
    volatile raw32_t sub_b[] = {
        0x33000000UL, 0x33c00000UL, 0x33800000UL,
        0x3f7fffffUL, 0x00ffffffUL
    };

    okbits("add below half", frombits(add_a[0]) + frombits(add_b[0]),
           0x3f800000UL);
    okbits("add tie even", frombits(add_a[1]) + frombits(add_b[1]),
           0x3f800000UL);
    okbits("add tie odd", frombits(add_a[2]) + frombits(add_b[2]),
           0x3f800002UL);
    okbits("add above half", frombits(add_a[3]) + frombits(add_b[3]),
           0x3f800001UL);
    okbits("add negative", frombits(add_a[4]) + frombits(add_b[4]),
           0xbf800001UL);
    okbits("add exponent carry", frombits(add_a[5]) + frombits(add_b[5]),
           0x40000000UL);
    okbits("add gap16 sticky", frombits(add_a[6]) + frombits(add_b[6]),
           0x3f800081UL);
    okbits("add gap24 sticky", frombits(add_a[7]) + frombits(add_b[7]),
           0x3f800001UL);

    okbits("sub tie", frombits(sub_a[0]) - frombits(sub_b[0]),
           0x3f800000UL);
    okbits("sub round", frombits(sub_a[1]) - frombits(sub_b[1]),
           0x3f7ffffeUL);
    okbits("sub cancellation", frombits(sub_a[2]) - frombits(sub_b[2]),
           0x3f800000UL);
    okbits("sub adjacent binade", frombits(sub_a[3]) - frombits(sub_b[3]),
           0x33800000UL);
    okbits("sub extension only", frombits(sub_a[4]) - frombits(sub_b[4]),
           0x00000001UL);

    okbits("mul below half", frombits(mul_a[0]) * frombits(mul_b[0]),
           0x3f800ffeUL);
    okbits("mul tie even", frombits(mul_a[1]) * frombits(mul_b[1]),
           0x3fbe6c18UL);
    okbits("mul tie odd", frombits(mul_a[2]) * frombits(mul_b[2]),
           0x3fc1f65eUL);
    okbits("mul above half", frombits(mul_a[3]) * frombits(mul_b[3]),
           0x3f8016cdUL);
    okbits("mul negative", frombits(mul_a[4]) * frombits(mul_b[4]),
           0xbf8016cdUL);
    okbits("mul exponent carry", frombits(mul_a[5]) * frombits(mul_b[5]),
           0x40000000UL);
    okbits("mul overflow", frombits(mul_a[6]) * frombits(mul_b[6]),
           0x7f800000UL);
    okbits("mul max finite", frombits(mul_a[7]) * frombits(mul_b[7]),
           0x7f7fffffUL);
    okbits("fmadd plus zero", fmadd0(frombits(mul_a[3]),
                                    frombits(mul_b[3])),
           0x3f8016cdUL);
    okbits("fmadd overflow", fmadd0(frombits(mul_a[6]),
                                   frombits(mul_b[6])),
           0x7f800000UL);
    okbits("fmadd max finite", fmadd0(frombits(mul_a[7]),
                                     frombits(mul_b[7])),
           0x7f7fffffUL);

    if (failures == 0)
        printf("TFRNDOP PASS %d\n", checks);
    return failures != 0;
}
