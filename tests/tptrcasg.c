/* Compound assignment through pointers and arrays declared in nested blocks.
 *
 * Block-scope locals have no symbol when their statements are lowered, so the
 * target type of `*q op= x` and `q[i] op= x` is only known once the function's
 * deferred metadata is repaired. These shapes once added only the low word of
 * a 32-bit right-hand side (BUG-068). Function-scope forms are the control.
 * Baseline proven with clang.
 */
#include <stdio.h>
#include <stdint.h>

int32_t a3[4] = { 15, 271, 63, -128 };
static struct { int32_t m0; } s0 = { 16711935 };
static int32_t g = 16711935;

static void function_scope(void)
{
    int32_t cell = 15;
    int32_t *q = &cell;
    *q += s0.m0;
    printf("fs %ld\n", (long)cell);
}

static void deref_forms(void)
{
    { int32_t *q = &a3[0]; (*q) += s0.m0; }
    { int32_t *q = &a3[1]; (*q) += g; }
    { int32_t *q = &a3[2]; (*q) += (int32_t)65536; }
    { int32_t *q = &a3[3]; (*q) -= s0.m0; }
    printf("deref %ld %ld %ld %ld\n", (long)a3[0], (long)a3[1], (long)a3[2], (long)a3[3]);
}

static void other_forms(void)
{
    { int32_t arr[2] = { 15, 7 }; arr[0] += g; arr[1] -= g; printf("arr %ld %ld\n", (long)arr[0], (long)arr[1]); }
    { int32_t *q = a3; q[1] += g; printf("idx %ld\n", (long)a3[1]); }
    { int32_t *q = a3; int i = 3; q[i] -= g; printf("vidx %ld\n", (long)a3[3]); }
    { struct { int32_t m; } s = { 15 }; s.m += g; printf("mbr %ld\n", (long)s.m); }
    { int32_t c = 15; c += g; printf("local %ld\n", (long)c); }
    { float f = 1.5f; float *pf = &f; *pf += 2.25f; printf("flt %d\n", (int)(f * 100)); }
    { uint8_t b = 250; uint8_t *pb = &b; *pb += 10; printf("byte %d\n", b); }
    { int16_t h = 300; int16_t *ph = &h; *ph *= 200; printf("half %d\n", h); }
    { int32_t *q = &a3[2]; *q *= 3; (*q) |= 0x10000L; printf("mulor %ld\n", (long)a3[2]); }
    { int32_t *q = &a3[3]; *q <<= 8; printf("shl %ld\n", (long)a3[3]); }
    { int32_t *q = &a3[0]; int32_t r = (*q += g); printf("value %ld %ld\n", (long)r, (long)a3[0]); }
    { uint32_t u = 0x80000001UL; uint32_t *pu = &u; *pu >>= 1; *pu ^= 0xffff0000UL; printf("uns %lu\n", (unsigned long)u); }
    { int32_t *q = &a3[1]; *q &= 0x00ff00ffL; *q %= 1000; *q /= 3; printf("andmod %ld\n", (long)a3[1]); }
}

static void read_forms(void)
{
    uint32_t hash = 2166136261UL;
    int32_t sum = 0;
    size_t k;
    { const uint8_t *b = (const uint8_t *)a3; for (k = 0; k < sizeof a3; ++k) hash = (hash ^ b[k]) * 16777619UL; }
    { const int16_t *h = (const int16_t *)a3; for (k = 0; k < 4; ++k) sum += h[k]; }
    { uint8_t *b = (uint8_t *)&a3[2]; b[0] ^= 0x55; b[3] += 1; }
    printf("read %lu %ld %ld\n", (unsigned long)hash, (long)sum, (long)a3[2]);
}

int main(void)
{
    deref_forms();
    function_scope();
    other_forms();
    read_forms();
    return 0;
}
