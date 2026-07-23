#include <stdio.h>

static inline int helper_add(int a, int b)
{
    return a + b;
}

static inline int helper_sub(int a, int b)
{
    return a - b;
}

static inline int scale3(int x);

static inline int scale3(int x)
{
    return helper_add(x, helper_add(x, x));
}

/* Regression test for a real dcc bug (fixed in emit_inline_arg_temps):
 * a call whose args are (base, esz, idx, v), where esz and idx are each
 * used more than once in the body, used to have ALL FOUR arguments routed
 * through temp locals as soon as ANY ONE of them (here, v) had an argument
 * that wasn't cheaply re-evaluable (a call to next_val(), which also has a
 * visible side effect via a counter, forcing that argument through a temp
 * to guarantee single evaluation) - including esz and idx, even though
 * their own arguments here are always compile-time literals that would
 * otherwise fold idx * esz to a shift instead of a runtime multiply. The
 * fix makes that decision per-parameter: a sibling needing a temp must not
 * drag an unrelated parameter's literal argument through one too. This
 * doesn't change the result (mem_set here just packs a value into a
 * byte/word slot) - only whether the compiler can prove esz constant - so
 * a plain correctness check can't catch a regression on its own; this
 * test's cycle count is tracked in tests/perf_baselines.csv specifically
 * so a reintroduced runtime multiply per call shows up as a measurable
 * perf regression, not just silently-slower code. */
static unsigned char fold_mem[64];
static int fold_counter;

static int next_val(void)
{
    fold_counter += 7;
    return fold_counter;
}

static inline void mem_set(int base, int esz, int idx, int v)
{
    if (esz == 1) {
        fold_mem[base + idx * esz] = (unsigned char)v;
    } else {
        fold_mem[base + idx * esz] = (unsigned char)(v & 255);
        fold_mem[base + idx * esz + 1] = (unsigned char)((v >> 8) & 255);
    }
}

static inline int mem_get(int base, int esz, int idx)
{
    if (esz == 1) return fold_mem[base + idx * esz];
    return (short)(fold_mem[base + idx * esz] | (fold_mem[base + idx * esz + 1] << 8));
}

static int inline_fold_check(void)
{
    int i, sum;

    fold_counter = 0;
    for (i = 0; i < 8; i++)
        mem_set(0, 2, i, next_val());
    mem_set(20, 1, 3, 0xAB);

    sum = 0;
    for (i = 0; i < 8; i++)
        sum += mem_get(0, 2, i);
    sum += mem_get(20, 1, 3);
    return sum;
}

int main(void)
{
    printf("static inline: %d %d\n", scale3(7), helper_sub(helper_add(10, 5), 3));
    printf("inline fold check: %d\n", inline_fold_check());
    return 0;
}