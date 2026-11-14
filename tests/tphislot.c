/* Regression for loop-live conditional PHI sources and backend slot lifetimes.
 * Covers folded and nonconstant conditions, float/long parameters, for/while
 * loops, accumulators and array/member stores, with stable-source controls. */
#include <stdio.h>
#include <stdint.h>

static float fa[4];
static long la[4];
static int ia[4];
static float g;

static void pf(const char *n, float f)
{
    union { float f; uint32_t u; } u;
    u.f = f;
    printf("%s %08lx\n", n, (unsigned long)u.u);
}

static void f1(float x) { int i; for (i = 0; i < 3; ++i) fa[1] = (0 ? 2.0f : x) - 1.0f; x = -1.0f; }
static void f2(float x) { int i; for (i = 0; i < 3; ++i) fa[1] = (1 ? x : 2.0f) - 1.0f; x = -1.0f; }
static void f3(float x) { int i; i = 0; while (i < 3) { fa[1] = (0 ? 2.0f : x) - 1.0f; i++; } x = g; }
static void f4(long x) { int i; for (i = 0; i < 3; ++i) la[1] = (0 ? 2L : x) - 1L; x = -1L; }
static void f5(int x) { int i; for (i = 0; i < 3; ++i) ia[1] = (0 ? 2 : x) - 1; x = -1; }
static void ok1(float x) { int i; for (i = 0; i < 3; ++i) fa[1] = (x + 0.0f) - 1.0f; x = -1.0f; }
static void ok2(float x) { float y = x; int i; for (i = 0; i < 3; ++i) fa[1] = (0 ? 2.0f : y) - 1.0f; y = -1.0f; }
static void ok3(float x) { int i; for (i = 0; i < 3; ++i) fa[1] = (0 ? 2.0f : x) - 1.0f; fa[2] = x; }

static struct { float f; long l; } member;
static void f6(float x, float c) { int i; for (i = 0; i < 3; ++i) fa[1] = (c ? x : 1.0f) - 1.0f; x = -1.0f; }
static float f7(float x) { float l = 10000.0f, c = 2.0f; int i; for (i = 0; i < 3; ++i) l -= (c ? x : 1.0f); x = 1.0f; return l; }
static long f8(long x) { long l = 25000L; int c = 1, i; for (i = 0; i < 3; ++i) l -= (c ? x : 16777216L); x = 1L; return l; }
static void f9(float x) { int i; for (i = 0; i < 3; ++i) member.f = (0 ? 2.0f : x) - 1.0f; x = -1.0f; }
static void f10(long x) { int i; i = 0; while (i < 3) { member.l = (1 ? x : 2L) - 1L; ++i; } x = -1L; }

int main(void)
{
    f1(-0.35f); pf("f1", fa[1]);
    f2(-0.35f); pf("f2", fa[1]);
    f3(-0.35f); pf("f3", fa[1]);
    f4(5L); printf("f4 %ld\n", la[1]);
    f5(5); printf("f5 %d\n", ia[1]);
    ok1(-0.35f); pf("ok1", fa[1]);
    ok2(-0.35f); pf("ok2", fa[1]);
    ok3(-0.35f); pf("ok3", fa[1]); pf("ok3", fa[2]);
    f6(-0.35f, 2.0f); pf("f6 true", fa[1]);
    f6(-0.35f, 0.0f); pf("f6 false", fa[1]);
    pf("f7", f7(5.0f)); printf("f8 %ld\n", f8(5L));
    f9(-0.35f); pf("f9", member.f);
    f10(5L); printf("f10 %ld\n", member.l);
    return 0;
}
