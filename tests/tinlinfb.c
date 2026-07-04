#include <stdio.h>

int counter = 0;
int slots[5];
int gcounter = 0;

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

static inline int plus5(int x)
{
    return x + 5;
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

static inline int local_helper(int x)
{
    int y;
    y = x + 4;
    return y;
}

static inline void store_add(int *dst, int v)
{
    *dst = *dst + v;
}

static inline void store_pair(int *dst, int a, int b)
{
    dst[0] = dst[0] + a;
    dst[1] = dst[1] + b;
}

/* Early-return if-chain whose final return expression has a side effect
 * (postfix increment on a global) - the increment must run exactly once
 * per call, in the returned expression's evaluation order. */
static inline int next_and_mark(void)
{
    if (gcounter > 100) return -1;
    return gcounter++;
}

/* If-branch body with a leading side-effect statement before its return -
 * folded into a comma expression rather than a bare return-or-ternary. */
static inline int bump_if(int k)
{
    if (k > 0) { gcounter++; return 1; }
    return 0;
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
    int stored2;
    int (*fp)(int);
    int via_ptr;
    int local_ok;
    int seq_ok;
    int bump_ok;

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
    slots[1] = 9;
    slots[2] = 20;
    store_pair(&slots[1], 3, 4);
    stored2 = slots[1] + slots[2];
    fp = plus5;
    via_ptr = fp(6);
    local_ok = local_helper(8);
    seq_ok = next_and_mark();
    seq_ok += next_and_mark() * 10;
    bump_ok = bump_if(5);
    bump_ok += bump_if(-1) * 10;
    printf("inline temps: %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", result, bigger, clamped, field, wide_ok, float_ok, stored, stored2, via_ptr, local_ok, counter, seq_ok, bump_ok, gcounter);
    return 0;
}