/* Constant float logical operands may contain unevaluated division by
 * zero (float oracle seed 206417). Cover discarded and consumed results,
 * nested logicals, compound assignment, and runtime guarding. */
#include <stdint.h>
#include <stdio.h>

static uint16_t hits;

static float touch(void)
{
    ++hits;
    return 2.0f;
}

static uint16_t guarded(uint16_t run)
{
    if (run) {
        (1.0f > 2.0f) || (1.0f / 0.0f);
        (1.0f < 2.0f) && (1.0f / 0.0f);
    }
    return 7;
}

int main(void)
{
    float value = 16.0f;
    ((-1.0f) < 123.456f) || (1e6f / 0.0f);
    (0.0f > 1.0f) && (1.0f / 0.0f);
    printf("logical %u %u\n",
           (uint16_t)((1.0f < 2.0f) || (1.0f / 0.0f)),
           (uint16_t)((1.0f > 2.0f) && (1.0f / 0.0f)));
    value /= ((-1.0f + 4) < 123.456f || (1e6f / 0.0f)) ? 2.0f : 4.0f;
    printf("compound %u\n", (uint16_t)value);
    printf("nested %u %u\n",
           (uint16_t)(((1.0f < 2.0f) || (1.0f / 0.0f)) && 1),
           (uint16_t)(((1.0f > 2.0f) && (1.0f / 0.0f)) || 0));
    (1.0f < 2.0f) || touch();
    (1.0f > 2.0f) && touch();
    (1.0f > 2.0f) || touch();
    (1.0f < 2.0f) && touch();
    printf("effects %u guarded %u\n", hits, guarded(0));
    printf("controls %u %u\n",
           (uint16_t)((1.0f > 2.0f) || (2.0f / 4.0f)),
           (uint16_t)((1.0f < 2.0f) && (2.0f / 4.0f)));
    return 0;
}
