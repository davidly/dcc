/* Integer divide/modulo zero-policy and signed overflow fallback coverage. */

#include <limits.h>
#include <stdio.h>

static int checks;
static int failures;
static volatile unsigned int zero16;
static volatile unsigned long zero32;
static volatile long minus_one = -1L;

static void cki(int got, int want, const char *name)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s got=%d want=%d\n", name, got, want);
    }
}

static void cku(unsigned int got, unsigned int want, const char *name)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s got=%u want=%u\n", name, got, want);
    }
}

static void ckl(long got, long want, const char *name)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s got=%ld want=%ld\n", name, got, want);
    }
}

static void ckul(unsigned long got, unsigned long want, const char *name)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s got=%lu want=%lu\n", name, got, want);
    }
}

static int s16div(int x, int y) { return x / y; }
static int s16mod(int x, int y) { return x % y; }
static unsigned int u16div(unsigned int x, unsigned int y) { return x / y; }
static unsigned int u16mod(unsigned int x, unsigned int y) { return x % y; }

static void s16pair(int x, int y, int *q, int *r)
{
    *q = x / y;
    *r = x % y;
}

static void u16pair(unsigned int x, unsigned int y,
                    unsigned int *q, unsigned int *r)
{
    *q = x / y;
    *r = x % y;
}

static long s32div(long x, long y) { return x / y; }
static long s32mod(long x, long y) { return x % y; }
static unsigned long u32div(unsigned long x, unsigned long y) { return x / y; }
static unsigned long u32mod(unsigned long x, unsigned long y) { return x % y; }

static void s32pair(long x, long y, long *q, long *r)
{
    *q = x / y;
    *r = x % y;
}

static void u32pair(unsigned long x, unsigned long y,
                    unsigned long *q, unsigned long *r)
{
    *q = x / y;
    *r = x % y;
}

static unsigned long mulmod(unsigned int a, unsigned int b, unsigned int m)
{
    return ((unsigned long)a * (unsigned long)b) % m;
}

int main(void)
{
    unsigned int z16 = zero16;
    unsigned long z32 = zero32;
    unsigned int uq16, ur16;
    int sq16, sr16;
    unsigned long uq32, ur32;
    long sq32, sr32;

    cku(u16div(0x8001U, z16), 0xffffU, "u16 div high");
    cku(u16mod(0x8001U, z16), 0x8001U, "u16 mod high");
    cki(s16div(-7, (int)z16), -1, "s16 div negative");
    cki(s16mod(-7, (int)z16), -7, "s16 mod negative");

    u16pair(0xffffU, z16, &uq16, &ur16);
    cku(uq16, 0xffffU, "u16 fused quotient");
    cku(ur16, 0xffffU, "u16 fused remainder");
    s16pair(INT_MIN, (int)z16, &sq16, &sr16);
    cki(sq16, -1, "s16 fused quotient");
    cki(sr16, INT_MIN, "s16 fused remainder");

    ckul(u32div(0x00000001UL, z32), 0xffffffffUL, "u32 div byte");
    ckul(u32mod(0x00000001UL, z32), 0x00000001UL, "u32 mod byte");
    ckul(u32div(0x01000001UL, z32), 0xffffffffUL, "u32 div shape");
    ckul(u32mod(0x01000001UL, z32), 0x01000001UL, "u32 mod shape");
    ckul(u32div(0x80000000UL, z32), 0xffffffffUL, "u32 div high");
    ckul(u32mod(0x80000000UL, z32), 0x80000000UL, "u32 mod high");
    ckl(s32div(-7L, (long)z32), -1L, "s32 div negative");
    ckl(s32mod(-7L, (long)z32), -7L, "s32 mod negative");

    u32pair(0xffffffffUL, z32, &uq32, &ur32);
    ckul(uq32, 0xffffffffUL, "u32 fused quotient");
    ckul(ur32, 0xffffffffUL, "u32 fused remainder");
    s32pair(LONG_MIN, (long)z32, &sq32, &sr32);
    ckl(sq32, -1L, "s32 fused quotient");
    ckl(sr32, LONG_MIN, "s32 fused remainder");
    s32pair(-7L, (long)z32, &sq32, &sr32);
    ckl(sq32, -1L, "s32 fused negative quotient");
    ckl(sr32, -7L, "s32 fused negative remainder");

    ckul(mulmod(0xffffU, 2U, z16), 0xfffeUL, "m1mu zero modulus");
    ckul(mulmod(0x8000U, 2U, z16), 0UL, "m1mu zero wrap");
    ckul(mulmod(0x8001U, 3U, z16), 0x8003UL,
         "m1mu zero low product");

    s32pair(LONG_MIN, minus_one, &sq32, &sr32);
    ckl(sq32, LONG_MIN, "LONG_MIN divide -1");
    ckl(sr32, 0L, "LONG_MIN modulo -1");

    printf("checks=%d failures=%d\n", checks, failures);
    puts(failures ? "RESULT: FAIL" : "RESULT: PASS");
    return failures != 0;
}
