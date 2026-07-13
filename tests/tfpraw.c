/*
 * tfpraw.c - direct verification of dccrtl's internal float predicate
 * helpers (__fzro, __fnan, __finf, __fsgn) and the int/long-to-float sign
 * dispatch (__fif, __flf), by calling them straight from raw IEEE-754 bit
 * patterns via inline asm.
 *
 * None of these are reachable from portable C: dcc has no isnan/isinf/
 * signbit front-end wiring, and __fsgn in particular is never called from
 * anywhere else in dccrtl.mac (or dcc's own codegen) at all, so this is the
 * only way to exercise it. Written to catch a regression in dccrtl.mac's
 * hand-written Z80, not as a portable test - it deliberately depends on the
 * DE:HL raw-float calling convention documented throughout dccrtl.mac.
 *
 * Wrapper names are kept short and distinct in their first 6 characters:
 * L80 truncates global symbols there, and longer look-alike names (e.g.
 * raw_iszero/raw_isnan/raw_isinf) silently collide into the same symbol.
 */
#include <stdio.h>

extern int fpzero(long bits);
extern int fpnan(long bits);
extern int fpinf(long bits);
extern int fpsgn(long bits);
extern long fpitof(int n);   /* __fif: int -> float, returned as raw bits */
extern long fpltof(long n);  /* __flf: long -> float, returned as raw bits */

#asm
	extrn __fzro
	extrn __fnan
	extrn __finf
	extrn __fsgn
	extrn __fif
	extrn __flf

	public _fpzero
_fpzero:
	push ix
	ld ix,0
	add ix,sp
	ld l,(ix+4)
	ld h,(ix+5)
	ld e,(ix+6)
	ld d,(ix+7)
	call __fzro
	ld sp,ix
	pop ix
	ret

	public _fpnan
_fpnan:
	push ix
	ld ix,0
	add ix,sp
	ld l,(ix+4)
	ld h,(ix+5)
	ld e,(ix+6)
	ld d,(ix+7)
	call __fnan
	ld sp,ix
	pop ix
	ret

	public _fpinf
_fpinf:
	push ix
	ld ix,0
	add ix,sp
	ld l,(ix+4)
	ld h,(ix+5)
	ld e,(ix+6)
	ld d,(ix+7)
	call __finf
	ld sp,ix
	pop ix
	ret

	public _fpsgn
_fpsgn:
	push ix
	ld ix,0
	add ix,sp
	ld l,(ix+4)
	ld h,(ix+5)
	ld e,(ix+6)
	ld d,(ix+7)
	call __fsgn
	ld sp,ix
	pop ix
	ret

	public _fpitof
_fpitof:
	push ix
	ld ix,0
	add ix,sp
	ld l,(ix+4)
	ld h,(ix+5)
	call __fif
	ld sp,ix
	pop ix
	ret

	public _fpltof
_fpltof:
	push ix
	ld ix,0
	add ix,sp
	ld l,(ix+4)
	ld h,(ix+5)
	ld e,(ix+6)
	ld d,(ix+7)
	call __flf
	ld sp,ix
	pop ix
	ret
#endasm

static int checks = 0;
static int failures = 0;

static void okb(const char *name, int got, int want)
{
    checks++;
    if ((got != 0) != (want != 0)) {
        failures++;
        printf("FAIL %s: got %d want %d\n", name, got, want);
    }
}

static void okl(const char *name, long got, long want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s: got %08lX want %08lX\n", name, (unsigned long)got, (unsigned long)want);
    }
}

int main(void)
{
    /* __fzro: +0.0 and -0.0 are zero; a normal value and both infinities
     * and a NaN are not. */
    okb("iszero +0.0", fpzero(0x00000000L), 1);
    okb("iszero -0.0", fpzero(0x80000000L), 1);
    okb("iszero +1.5", fpzero(0x3FC00000L), 0);
    okb("iszero -1.5", fpzero(0xBFC00000L), 0);
    okb("iszero +Inf", fpzero(0x7F800000L), 0);
    okb("iszero NaN",  fpzero(0x7FC00000L), 0);

    /* __fnan: only exponent-all-ones with nonzero mantissa. Infinity
     * (mantissa zero) and exponent 254 (mantissa nonzero, still finite)
     * must both read as false - the second is exactly what suggestion #7
     * discussed and is the sharpest edge case for this predicate. */
    okb("isnan +0.0", fpnan(0x00000000L), 0);
    okb("isnan +1.5", fpnan(0x3FC00000L), 0);
    okb("isnan +Inf", fpnan(0x7F800000L), 0);
    okb("isnan -Inf", fpnan(0xFF800000L), 0);
    okb("isnan qNaN", fpnan(0x7FC00000L), 1);
    okb("isnan sNaN", fpnan(0x7F800001L), 1);
    okb("isnan exp254", fpnan(0x7F7FFFFFL), 0);

    /* __finf: only exponent-all-ones with zero mantissa. */
    okb("isinf +0.0", fpinf(0x00000000L), 0);
    okb("isinf +1.5", fpinf(0x3FC00000L), 0);
    okb("isinf +Inf", fpinf(0x7F800000L), 1);
    okb("isinf -Inf", fpinf(0xFF800000L), 1);
    okb("isinf qNaN", fpinf(0x7FC00000L), 0);
    okb("isinf exp254", fpinf(0x7F7FFFFFL), 0);

    /* __fsgn: pure sign-bit test, regardless of magnitude/class. */
    okb("signbit +0.0", fpsgn(0x00000000L), 0);
    okb("signbit -0.0", fpsgn(0x80000000L), 1);
    okb("signbit +1.5", fpsgn(0x3FC00000L), 0);
    okb("signbit -1.5", fpsgn(0xBFC00000L), 1);
    okb("signbit +Inf", fpsgn(0x7F800000L), 0);
    okb("signbit -Inf", fpsgn(0xFF800000L), 1);
    okb("signbit NaN",  fpsgn(0x7FC00000L), 0);

    /* __fif (int -> float sign dispatch) and __flf (long -> float sign
     * dispatch): regression-check both branches of each against dcc's own
     * already-trusted (float) cast, via the raw bit pattern (a union, so
     * this doesn't depend on any float printf/compare machinery that could
     * itself route through these same routines). */
    {
        union { float f; long l; } u;
        u.f = (float)0;      okl("iftof 0", fpitof(0), u.l);
        u.f = (float)1;      okl("iftof 1", fpitof(1), u.l);
        u.f = (float)100;    okl("iftof 100", fpitof(100), u.l);
        u.f = (float)-1;     okl("iftof -1", fpitof(-1), u.l);
        u.f = (float)-100;   okl("iftof -100", fpitof(-100), u.l);
        u.f = (float)32767;  okl("iftof 32767", fpitof(32767), u.l);
        u.f = (float)-32768; okl("iftof -32768", fpitof(-32768), u.l);

        u.f = (float)0L;             okl("lftof 0", fpltof(0L), u.l);
        u.f = (float)1L;             okl("lftof 1", fpltof(1L), u.l);
        u.f = (float)-1L;            okl("lftof -1", fpltof(-1L), u.l);
        u.f = (float)1000000L;       okl("lftof 1000000", fpltof(1000000L), u.l);
        u.f = (float)-1000000L;      okl("lftof -1000000", fpltof(-1000000L), u.l);
        u.f = (float)2147483647L;    okl("lftof LONG_MAX", fpltof(2147483647L), u.l);
        u.f = (float)(-2147483647L - 1L);
        okl("lftof LONG_MIN", fpltof(-2147483647L - 1L), u.l);
    }

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
