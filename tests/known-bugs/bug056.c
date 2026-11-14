/* BUG-056: latent wrong-code exposed while admitting float update values
 * for BUG-041. Negating a member-array or block-local float ++/-- result
 * uses integer negation/narrowing
 * instead of IEEE sign-bit negation. The admission experiment is not shipped;
 * until BUG-041 is safely fixed this reproducer is rejected with DCC-E1002. */
#include <stdint.h>
#include <stdio.h>

static float out[2];
static float scalar = 3.14159f;
static struct S { float pad; float other; uint16_t word; float a[2]; } s[2] = {
    { 1.0f, 2.0f, 3, { 4.0f, 5.0f } },
    { 1.0f, 2.0f, 3, { 4.0f, 3.14159f } }
};

static uint32_t bits(float f)
{
    union { float f; uint32_t u; } value;
    value.f = f;
    return value.u;
}

static float block_value(void)
{
    float result = -274.26346f;
    {
        float local = (uint16_t)0;
        result = -(--local);
    }
    return result;
}

int main(void)
{
    out[1] = -s[1].a[1];
    printf("plain %08lx\n", (unsigned long)bits(out[1]));
    { float local = 1.0f; out[1] = -(s[1].a[1]++); }
    printf("member post %08lx %08lx\n", (unsigned long)bits(out[1]),
           (unsigned long)bits(s[1].a[1]));
    out[1] = -(--s[1].a[1]);
    printf("member pre %08lx %08lx\n", (unsigned long)bits(out[1]),
           (unsigned long)bits(s[1].a[1]));
    out[1] = -(scalar++);
    printf("scalar post %08lx %08lx\n", (unsigned long)bits(out[1]),
           (unsigned long)bits(scalar));
    out[1] = -(--scalar);
    printf("scalar pre %08lx %08lx\n", (unsigned long)bits(out[1]),
           (unsigned long)bits(scalar));
    printf("block pre %08lx\n", (unsigned long)bits(block_value()));
    return 0;
}
