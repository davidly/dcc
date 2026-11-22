/* Shapes the oracle fuzzer's union, comma and pointer-difference features
 * found miscompiled:
 *   - a source division of a char pointer difference was emitted as the
 *     exact stride shift, flooring (p - q) / 8 instead of truncating;
 *   - a one-byte struct or union argument was pushed in one byte while the
 *     callee laid its parameters out in two-byte slots;
 *   - (x, bf) lost the bit-field's promotion to int, so (1, bf) > -129
 *     compared unsigned;
 *   - (x, v) on a nested-block local kept the type it had before the local
 *     was resolved, so 32-bit comparisons used the wrong width. */
#include <stdio.h>
#include <stdint.h>

struct B { uint16_t lo : 11; uint16_t hi : 5; };
struct T1 { uint8_t a; };
union U1 { uint8_t a; uint8_t b; };

int8_t bytes[4] = { 1, 2, 3, 4 };
int16_t neg[2] = { 9029, -129 };
int32_t words[4] = { 10, 20, 30, 40 };
uint32_t wide[2] = { 0xaae49534UL, 5 };
struct T1 gt = { 8 };
union U1 gu = { 7 };
static int failures;

static void check(const char *what, long got, long want)
{
    if (got != want) {
        printf("%s: got %ld, want %ld\n", what, got, want);
        ++failures;
    }
}

static int16_t pdiv(int8_t *p, int8_t *q)
{
    return (int16_t)(p - q) / 8;
}

static int16_t cdiv(char *p, char *q)
{
    int d = p - q;
    return d / 4;
}

static int16_t pt(struct T1 x, int32_t *p)
{
    return (int16_t)(p[1] + x.a);
}

static int16_t pu(union U1 x, int16_t y)
{
    return y + x.a;
}

static uint32_t pw(union U1 x, struct T1 y)
{
    return (uint32_t)x.a * 256u + y.a;
}

static struct T1 mk(uint8_t v)
{
    struct T1 t;
    t.a = v;
    return t;
}

static int16_t (*fp)(union U1, int16_t) = pu;

static int bfcomma(void)
{
    struct B t = { 402, 0 };
    return (1, t.lo) > neg[1];
}

static int bfdiff(void)
{
    struct B t = { 402, 0 };
    return ((1, t.lo) - 500) < 0;
}

static int nested_short(void)
{
    int r = 0;
    {
        short b = (uint16_t)bytes[0] - 3;
        if (((int32_t)1) <= (1, b))
            r = 1;
    }
    return r;
}

static int nested_wide(void)
{
    int r = 0;
    {
        uint32_t *q = &wide[0];
        if ((((uint16_t)1), (*q)) > ((uint16_t)(~((uint16_t)1))))
            r = 1;
    }
    return r;
}

int main(void)
{
    check("pdiv", pdiv(bytes, bytes + 2), 0);
    check("pdiv+", pdiv(bytes + 4, bytes), 0);
    check("cdiv", cdiv((char *)bytes, (char *)bytes + 3), 0);
    check("struct arg", pt(gt, &words[1]), 38);
    check("union arg", pu(gu, 100), 107);
    check("two args", (long)pw(gu, gt), 7L * 256 + 8);
    check("returned arg", pu(gu, pt(mk(3), &words[0])), 30);
    check("pointer call", fp(gu, 6), 13);
    check("bit-field comma", bfcomma(), 1);
    check("bit-field comma diff", bfdiff(), 1);
    check("nested short", nested_short(), 0);
    check("nested wide", nested_wide(), 1);
    if (failures == 0)
        printf("tcommapd passed\n");
    return 0;
}
