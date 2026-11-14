/* Stores to an escaped float parameter remain visible through its pointer
 * after counted-loop local labels. Covers nested/conditional/plain blocks,
 * direct reads, pointer stores, and a no-loop control using float bit patterns. */
#include <stdint.h>
#include <stdio.h>

static float g = 0.0f;

/* the loop in a nested block before the assignment is what matters */
static float with_block_loop(float x0)
{
    float *p = &x0;
    if ((int32_t)12) {
        int16_t i;
        for (i = 0; i < 2; ++i) {}
    }
    x0 = 1.0f;
    return *p;
}

static float with_float_test(float x0)
{
    float *p = &x0;
    if (g == 0.0f) {
        int16_t i;
        for (i = 0; i < 2; ++i) {}
    }
    x0 = 1.0f;
    return *p;
}

static float with_plain_block(float x0)
{
    float *p = &x0;
    {
        int16_t i;
        for (i = 0; i < 2; ++i) {}
    }
    x0 = 1.0f;
    return *p;
}

/* no loop: right */
static float no_loop(float x0)
{
    float *p = &x0;
    x0 = 1.0f;
    return *p;
}

/* read the parameter itself: right */
static float read_param(float x0)
{
    float *p = &x0;
    {
        int16_t i;
        for (i = 0; i < 2; ++i) {}
    }
    x0 = 1.0f;
    return x0 + (*p - *p);
}

/* store through the pointer and read the parameter: right */
static float store_through(float x0)
{
    float *p = &x0;
    {
        int16_t i;
        for (i = 0; i < 2; ++i) {}
    }
    *p = 1.0f;
    return x0;
}

static void show(const char *name, float v)
{
    union { float f; uint32_t u; } c;
    c.f = v;
    printf("%s %08lx\n", name, (unsigned long)c.u);
}

int main(void)
{
    show("block_loop ", with_block_loop(0.3f));   /* want 3f800000 */
    show("float_test ", with_float_test(0.3f));   /* want 3f800000 */
    show("plain_block", with_plain_block(0.3f));  /* want 3f800000 */
    show("no_loop    ", no_loop(0.3f));
    show("read_param ", read_param(0.3f));
    show("store_thru ", store_through(0.3f));
    return 0;
}
