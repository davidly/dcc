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

/* Regression test for a real dcc bug (fixed in inline_call_needs_arg_temps /
 * emit_inline_arg_temps): a stack-shaped push/pop pair, both static inline,
 * where push's own body has a side effect (vsp2++, part of its LHS array
 * index) sequenced textually before its parameter's one use. Calling
 * push(!pop()) with both inlined used to clone to vstack[vsp2++] = !pop(),
 * and dcc's assignment codegen evaluated the LHS address - committing
 * vsp2++ to memory - before the RHS, so pop()'s own vsp2-- then read the
 * wrong slot. This doesn't change *what* gets computed once codegen order
 * is corrected, so a plain correctness check can't catch a regression on
 * its own; this test's cycle count is tracked in tests/perf_baselines.csv
 * so a reintroduced unsafe direct-substitution shows up as a measurable
 * perf change (fewer or more inline temps), not just silently-wrong output
 * that happens to still be caught here by the return value. */
static int vstack[8];
static int vsp2;

static inline int vpop2(void)
{
    return vstack[--vsp2];
}

static inline void vpush2(int v)
{
    vstack[vsp2++] = v;
}

static int inline_order_check(void)
{
    int a, b;

    vsp2 = 0;
    vpush2(0);
    vpush2(5);
    b = vpop2();
    a = vpop2();
    vpush2(a == b);
    vpush2(!vpop2());
    return vpop2();
}

/* Regression test for a related fix (removing emit_inline_arg_temps' bail
 * out when the callee's own body contains a nested inline call, plus
 * teaching function_body_mentions_multiuse_inline_call's lexical pre-scan
 * about call_args_may_need_temps so #itmpN locals actually get reserved for
 * this shape): a call whose argument is itself a call to a static inline
 * function that calls a SECOND static inline function, where the outer
 * call's temp and the inner call's temp land at the SAME parameter index
 * (1) in their respective functions. Before the fix this either lost
 * inlining entirely for scale_and_clamp/clamp4 (falling back to real calls
 * - a large, unnecessary cycle cost, not a correctness bug on its own) or,
 * with only the pre-scan half of the fix missing, hit a NULL from
 * find_local for the un-reserved #itmp1 slot. The #itmpN slots are
 * per-function, not per-nesting-level, so this also stands as a check that
 * reusing a slot NUMBER across nesting levels is safe: each slot is
 * written, immediately consumed once, and only ever revisited after its
 * prior value has already been fully read - see emit_inline_arg_temps'
 * comment for why that ordering can't collide. */
static int nestbuf[8];
static int nestptr;

static inline int nest_take(void)
{
    return nestbuf[--nestptr];
}

static inline void nest_give(int v)
{
    nestbuf[nestptr++] = v;
}

static inline int nest_clamp4(int value)
{
    if (value > 100) return 100;
    if (value < -100) return -100;
    if (value < 0) return -value;
    return value;
}

static inline int nest_scale_and_clamp(int left, int right)
{
    return nest_clamp4(left * right);
}

static int inline_nest_check(void)
{
    nestptr = 0;
    nest_give(10);
    nest_give(4);
    nest_give(nest_scale_and_clamp(3, nest_take()));
    return nest_take();
}

/* A real call evaluates every argument exactly once before entering the
 * callee, even when a parameter is unused, appears only in a short-circuited
 * expression, or appears only as the operand of sizeof. */
static int edge_calls;

static int edge_bump(void)
{
    edge_calls++;
    return 7;
}

static inline int edge_unused(int unused, int value)
{
    return value;
}

static inline int edge_conditional(int flag, int value)
{
    return flag ? 99 : value;
}

static inline int edge_and(int flag, int value)
{
    return flag && value;
}

static inline int edge_or(int flag, int value)
{
    return flag || value;
}

static inline int edge_sizeof(int value)
{
    return sizeof(value) != 0;
}

static int inline_eval_check(void)
{
    int sum;

    edge_calls = 0;
    sum = edge_unused(edge_bump(), 5);
    sum += edge_conditional(1, edge_bump());
    sum += edge_and(0, edge_bump());
    sum += edge_or(1, edge_bump());
    sum += edge_sizeof(edge_bump());
    return sum * 10 + edge_calls;
}

/* Volatile accesses are observable and must not be duplicated when an inline
 * parameter is used more than once. */
static volatile int edge_volatile;

static inline int edge_twice(int value)
{
    return value + value;
}

static int inline_volatile_check(void)
{
    edge_volatile = 7;
    return edge_twice(edge_volatile);
}

/* A side-effect-free inline body that READS a global is still unsafe to
 * substitute a side-effecting argument into without a temp: the argument's
 * write must be committed before the body's read, exactly as a real call
 * would. edge_rw_read's body reads edge_rw_global; the argument writes it to
 * 100 and yields 1, so the result must be 101 (temp materialized first), not
 * 1 (body read reordered before the write). */
static int edge_rw_global;

static inline int edge_rw_read(int value)
{
    return edge_rw_global + value;
}

static int inline_readwrite_check(void)
{
    edge_rw_global = 0;
    return edge_rw_read((edge_rw_global = 100, 1));
}

/* Argument evaluation also includes non-volatile READS. A real call reads
 * edge_rw_global before write_then_value enters and writes it; direct AST
 * substitution must not move either a nested pure call or a plain global read
 * after that write. */
static inline int edge_read_global(int addend)
{
    return edge_rw_global + addend;
}

static inline int edge_write_then_value(int value)
{
    return (edge_rw_global = 10, value);
}

static int inline_read_order_check(void)
{
    int call_result;
    int ident_result;

    edge_rw_global = 3;
    call_result = edge_write_then_value(edge_read_global(0));
    edge_rw_global = 4;
    ident_result = edge_write_then_value(edge_rw_global);
    return call_result * 10 + ident_result;
}

/* Outer temp arguments are all materialized before the cloned body consumes
 * them. A nested inline call in a later argument must not reuse and overwrite
 * an already-live outer #itmpN slot. */
static int edge_inner_count;
static int edge_outer_count;
static int edge_sink[4];

static int edge_next_inner(void)
{
    edge_inner_count++;
    return 11;
}

static int edge_next_outer(void)
{
    edge_outer_count++;
    return 7;
}

static inline int edge_inner(int zero, int value)
{
    edge_inner_count++;
    return value + zero;
}

static inline void edge_outer(int left, int right)
{
    edge_sink[edge_outer_count++] = left - right;
}

static inline int edge_outer_body(int unused, int right)
{
    return edge_inner(0, edge_next_inner()) + right;
}

static int inline_temp_collision_check(void)
{
    int body_value;

    edge_inner_count = 0;
    edge_outer_count = 0;
    edge_outer(edge_inner(0, edge_next_inner()), edge_next_outer());
    body_value = edge_outer_body(edge_next_outer(), edge_next_outer());
    return edge_sink[1] * 1000 + body_value * 100 +
           edge_inner_count * 10 + edge_outer_count;
}

int main(void)
{
    printf("static inline: %d %d\n", scale3(7), helper_sub(helper_add(10, 5), 3));
    printf("inline fold check: %d\n", inline_fold_check());
    printf("inline order check: %d\n", inline_order_check());
    printf("inline nest check: %d\n", inline_nest_check());
    printf("inline eval check: %d\n", inline_eval_check());
    printf("inline volatile check: %d\n", inline_volatile_check());
    printf("inline temp collision check: %d\n", inline_temp_collision_check());
    printf("inline readwrite check: %d\n", inline_readwrite_check());
    printf("inline read order check: %d\n", inline_read_order_check());
    return 0;
}