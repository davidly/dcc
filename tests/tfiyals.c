/* IY spill-base reads observe stores to the same IX frame (float oracle
 * seed 206531). The retained declarations and nested scopes give MIR a
 * frame larger than 128 bytes and exercise reused spill slots. */
#include <stdint.h>
#include <stdio.h>
struct S { int16_t m1; float m3, m0, m2[2]; };
static struct S s0;
static float large_frame(void)
{
    int16_t l0 = 7;
    float la1[4] = { 1885.3275f, 0.25f, (-61018.0f), 1005.05786f };
    uint16_t l2 = 2;
    uint8_t l3 = 88;
    float la4[4] = { 19.044943f, 32767.0f, (-2.5f), 0.0625f };
    int16_t n2 = 3;
    float w3 = 7.4099665f;
    int16_t n9 = 3;
    float w10 = 27.001741f;
    int16_t i11;
    int16_t n18 = 4;
    float w19 = -14654.77f;
    int16_t n20 = 1;
    float w21 = -1.0f;
    int16_t n24 = 2;
    float w25 = 1.5328705f;
    int16_t n26 = 3;
    float w27 = 2.71828f;
    int16_t n28 = 2;
    float w29 = 0.582f;
    if (0) {
        {
            float b1 = 16777216.0f;
        }
        for (int16_t i4 = 0; i4 < 4; ++i4) {
        }
    } else {
        for (int16_t i5 = 0; i5 < 5; ++i5) {
        }
        for (int16_t i6 = 0; i6 < 3; ++i6) {
            s0.m2[1] = 32768.0f;
            {
                uint16_t b7 = (uint16_t)1;
                float b8 = 1.0f;
                b8 = (2.0f + 65536.0f);
            }
        }
    }
    for (i11 = 0; i11 < 6; ++i11) {
        {
            float b12 = 1.0f;
            {
                float b13 = 1.0f;
            }
            {
                float b14 = 1.0f;
                float b15 = 1.0f;
            }
        }
    }
    {
        float b22 = 1.0f;
        float b23 = 1.0f;
        la4[1] = 1;
    }
    {
        float b30 = 1.0f;
        float b31 = 1.0f;
        {
            float b32 = 1.0f;
            int16_t b33 = 1;
        }
    }
    for (int16_t i34 = 0; i34 < 2; ++i34) {
        la4[l2 & 3] = 1.0f;
    }
    return (uint32_t)1;
}

static uint32_t small_frame(void)
{
    float value = 32768.0f;
    s0.m2[0] = value;
    value = 2.0f + 65536.0f;
    return (uint32_t)value;
}

int main(void)
{
    float result = large_frame();
    printf("large %u %lu\n", (uint16_t)result,
           (unsigned long)(uint32_t)s0.m2[1]);
    printf("control %lu\n", (unsigned long)small_frame());
    printf("stored %lu\n", (unsigned long)(uint32_t)s0.m2[0]);
    return 0;
}
