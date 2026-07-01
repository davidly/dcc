#include <stdio.h>

int counter = 0;
int slots[5];

struct Pair {
    int left;
    int right;
};

static int bump(void)
{
    counter++;
    return counter;
}

static inline int twice(int x)
{
    return x + x;
}

static inline int max2(int a, int b)
{
    return a > b ? a : b;
}

static inline int clamp8(int x)
{
    if (x < 0)
        return 0;
    if (x > 255)
        return 255;
    return x;
}

static inline int pair_right(struct Pair *p)
{
    return p->right;
}

static inline long add_long(long a, long b)
{
    return a + b + 100000L;
}

static inline float half_plus_one(float x)
{
    return x * 0.5 + 1.0;
}

static inline void store_add(int *dst, int v)
{
    *dst = *dst + v;
}

int main(void)
{
    struct Pair pair;
    struct Pair *pp;
    int result;
    int bigger;
    int clamped;
    int field;
    long wide;
    int wide_ok;
    float fwide;
    int float_ok;
    int stored;

    pair.left = 44;
    pair.right = 77;
    pp = &pair;

    result = twice(bump());
    bigger = max2(bump(), 10);
    clamped = clamp8(bump() + 300);
    field = pair_right(pp);
    wide = add_long(30000L, 12000L);
    wide_ok = (wide == 142000L);
    fwide = half_plus_one(13.0);
    float_ok = (fwide > 7.4 && fwide < 7.6);
    slots[4] = 30;
    store_add(&slots[bump()], 12);
    stored = slots[4];
    printf("inline temps: %d %d %d %d %d %d %d %d\n", result, bigger, clamped, field, wide_ok, float_ok, stored, counter);
    return 0;
}