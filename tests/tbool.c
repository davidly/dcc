/* tbool.c - C99 _Bool and stdbool.h semantics. */
#include <stdio.h>
#include <stdbool.h>

struct Flags {
    _Bool a;
    unsigned char marker;
    bool b;
};

static int failures;
static int short_trace;
static _Bool global_true = 42;
static _Bool global_false = 0;
static _Bool global_arr[4] = { 0, 2, -3, 0 };
static struct Flags global_flags = { 5, 0x7e, 0 };

static void check_int(int got, int want, const char *name)
{
    if (got != want) {
        printf("FAIL %s got=%d want=%d\n", name, got, want);
        failures++;
    }
}

static _Bool bool_identity(_Bool value)
{
    return value;
}

static int bool_param_sum(_Bool a, _Bool b, _Bool c)
{
    return a + b + c;
}

static void set_bool(_Bool *p, int v)
{
    *p = v;
}

static int count_true(const _Bool *p, int n)
{
    int i;
    int total;

    total = 0;
    for (i = 0; i < n; i++) {
        if (p[i])
            total++;
    }
    return total;
}

static _Bool ternary_bool(int cond)
{
    return cond ? 88 : 0;
}

static int trace_bool(int tag)
{
    short_trace = short_trace * 10 + tag;
    return tag & 1;
}

static void check_short_circuit_phi(void)
{
    int result;
    int side;

    short_trace = 0;
    result = trace_bool(2) || trace_bool(4) || trace_bool(5);
    check_int(result, 1, "or true result");
    check_int(short_trace, 245, "or true short circuit");

    short_trace = 0;
    result = trace_bool(3) && trace_bool(5) && trace_bool(6);
    check_int(result, 0, "and false result");
    check_int(short_trace, 356, "and false short circuit");

    short_trace = 0;
    result = trace_bool(2) || trace_bool(4) || trace_bool(6);
    check_int(result, 0, "or false result");
    check_int(short_trace, 246, "or false full evaluation");

    short_trace = 0;
    result = trace_bool(3) && trace_bool(5) && trace_bool(7);
    check_int(result, 1, "and true result");
    check_int(short_trace, 357, "and true full evaluation");

    short_trace = 0;
    side = 0;
    result = (trace_bool(2) && (side = 1)) ||
             (trace_bool(3) && (side = 2)) ||
             trace_bool(5);
    check_int(result, 1, "side-effect result");
    check_int(short_trace, 23, "side-effect short circuit");
    check_int(side, 2, "side-effect merged value");
}

static void check_locals(void)
{
    _Bool a;
    _Bool b;
    bool c;
    _Bool arr[4];
    struct Flags flags;

    a = 0;
    b = 99;
    c = false;
    arr[0] = 0;
    arr[1] = 123;
    arr[2] = -4;
    arr[3] = (_Bool)0;
    flags.a = 1000L;
    flags.marker = 0x55;
    flags.b = 0;

    check_int(sizeof(_Bool), 1, "sizeof _Bool");
    check_int(sizeof(bool), 1, "sizeof bool");
    check_int(a, 0, "local false");
    check_int(b, 1, "local true");
    check_int(c, 0, "stdbool false");
    check_int(true, 1, "stdbool true");
    check_int(false, 0, "stdbool false macro");
    check_int(arr[0], 0, "arr[0]");
    check_int(arr[1], 1, "arr[1]");
    check_int(arr[2], 1, "arr[2]");
    check_int(arr[3], 0, "arr[3]");
    check_int(flags.a, 1, "flags.a");
    check_int(flags.marker, 0x55, "flags.marker");
    check_int(flags.b, 0, "flags.b");
}

static void check_globals(void)
{
    check_int(global_true, 1, "global_true");
    check_int(global_false, 0, "global_false");
    check_int(global_arr[0], 0, "global_arr[0]");
    check_int(global_arr[1], 1, "global_arr[1]");
    check_int(global_arr[2], 1, "global_arr[2]");
    check_int(global_arr[3], 0, "global_arr[3]");
    check_int(global_flags.a, 1, "global_flags.a");
    check_int(global_flags.marker, 0x7e, "global_flags.marker");
    check_int(global_flags.b, 0, "global_flags.b");
}

static void check_casts_and_params(void)
{
    long big;
    _Bool from_long;
    _Bool from_cast;

    big = 65536L;
    from_long = big;
    from_cast = (_Bool)big;

    check_int(from_long, 1, "from long assign");
    check_int(from_cast, 1, "from long cast");
    check_int((_Bool)0, 0, "cast zero");
    check_int((_Bool)-12, 1, "cast negative");
    check_int(bool_identity(7), 1, "param normalize");
    check_int(bool_identity(0), 0, "param zero");
    check_int(bool_param_sum(0, 2, -3), 2, "param sum");
}

static void check_deref_and_arrays(void)
{
    _Bool b;
    _Bool arr[3];

    set_bool(&b, 77);
    check_int(b, 1, "deref store nonzero");
    set_bool(&b, 0);
    check_int(b, 0, "deref store zero");

    arr[0] = 0;
    arr[1] = 55;
    arr[2] = 0;
    check_int(count_true(arr, 3), 1, "pointer iter count");
}

static void check_conditions(void)
{
    _Bool t;
    _Bool f;
    int hits;

    t = 9;
    f = 0;

    hits = 0;
    if (t) hits++;
    if (f) hits++;
    if (!f) hits++;
    if (!t) hits++;
    check_int(hits, 2, "if conditions");

    hits = 0;
    while (t) {
        hits++;
        t = 0;
    }
    check_int(hits, 1, "while condition");
}

static void check_operators(void)
{
    _Bool a;
    _Bool b;
    int x;

    a = 3;
    b = 0;

    check_int(a && !b, 1, "logical and/not");
    check_int(a || b, 1, "logical or");
    check_int(b || b, 0, "logical or false");

    x = 5;
    a = (x > 0);
    check_int(a, 1, "bool from relational");
    a = (x < 0);
    check_int(a, 0, "bool from relational false");

    a = 44;
    b = 0;
    check_int(a == b, 0, "bool != bool");
    a = 200;
    b = 100;
    check_int(a == b, 1, "bool == bool normalized");
}

static void check_compound_ops(void)
{
    _Bool b;

    b = 0;
    b |= 4;
    check_int(b, 1, "compound |=");

    b &= 2;
    check_int(b, 0, "compound &=");

    b = 1;
    b ^= 1;
    check_int(b, 0, "compound ^=");
}

static void check_varargs_and_ternary(void)
{
    _Bool b;
    char buf[8];

    b = 123;
    sprintf(buf, "%d", b);
    check_int(buf[0], '1', "varargs promote");
    check_int(buf[1], 0, "varargs single digit");

    check_int(ternary_bool(1), 1, "ternary true arm");
    check_int(ternary_bool(0), 0, "ternary false arm");
}

/* Keep these mutable and out of the initial-value checks above. */
static bool edge_left, edge_right;

static int branch_pair(void)
{
    int result = 0;
    if (edge_left) result += 1;
    if (!edge_left) result += 2;
    if (edge_right) result += 4;
    if (!edge_right) result += 8;
    if (edge_left && edge_right) result += 16;
    if (edge_left || edge_right) result += 32;
    if (!(edge_left && edge_right)) result += 64;
    if (!!edge_right) result += 128;
    return result;
}

static void check_branch_truth_table(void)
{
    int left, right, expected;
    for (left = 0; left < 2; ++left) {
        for (right = 0; right < 2; ++right) {
            edge_left = left;
            edge_right = right;
            expected = (left ? 1 : 2) + (right ? 4 : 8);
            if (left && right) expected += 16;
            if (left || right) expected += 32;
            if (!(left && right)) expected += 64;
            if (right) expected += 128;
            check_int(branch_pair(), expected, "global branch truth table");
        }
    }
}

static void check_branch_side_effects(void)
{
    bool saved;
    int hits = 0;

    edge_left = false;
    short_trace = 0;
    if (edge_left && trace_bool(3)) hits += 10;
    if (!edge_left || trace_bool(5)) hits++;
    check_int(short_trace, 0, "bool branches skip calls");
    check_int(hits, 1, "bool branches skip results");

    edge_left = true;
    short_trace = 0;
    if (edge_left && trace_bool(3)) hits += 2;
    if (!edge_left || trace_bool(5)) hits += 4;
    check_int(short_trace, 35, "bool branches call order");
    check_int(hits, 7, "bool branches call results");

    saved = edge_left;
    set_bool(&edge_left, 0);
    if (saved) hits += 8;
    if (edge_left) hits += 100;
    check_int(hits, 15, "saved bool survives alias write");
    check_int(saved + saved, 2, "branched bool remains normalized");

    set_bool(&saved, 0);
    if (!saved) hits += 16;
    set_bool(&saved, -17);
    if (saved) hits += 32;
    check_int(hits, 63, "local alias changes branch direction");
    check_int(saved, 1, "local alias store normalizes");
}

static void check_branch_loops(void)
{
    int count = 0, sum = 0;
    bool running = true;

    edge_left = true;
    while (edge_left) {
        ++count;
        if (count == 2) continue;
        sum += count;
        if (count == 4) edge_left = false;
        if (count > 5) break; /* Bound execution if an exit test regresses. */
    }
    check_int(count, 4, "global bool loop exit");
    check_int(sum, 8, "bool loop continue");

    count = 0;
    do {
        ++count;
        if (count == 3) running = false;
    } while (running && count < 5);
    check_int(count, 3, "local bool do loop exit");

    edge_right = false;
    for (count = 0; !edge_right && count < 5; ++count) {
        if (count == 2) edge_right = true;
    }
    check_int(count, 3, "negated bool for loop exit");
}

static void check_branch_merges(void)
{
    int i, selected, live;
    bool value;
    volatile bool observed;
    struct { bool first : 1; bool second : 1; } bits;

    for (i = 0; i < 4; ++i) {
        edge_left = i & 1;
        edge_right = i & 2;
        live = 1000 + i * 13;
        if (edge_left) selected = live + 7;
        else selected = live - 9;
        check_int(selected, 1000 + i * 13 + ((i & 1) ? 7 : -9),
                  "live word across bool branch and merge");
        value = edge_left ? edge_right : !edge_right;
        check_int(value, (i & 1) ? !!(i & 2) : !(i & 2),
                  "bool ternary merge");
        selected = value ? 37 : 91;
        check_int(selected, (((i & 1) ? !!(i & 2) : !(i & 2)) ? 37 : 91),
                  "branch on merged bool");

        observed = i;
        selected = 0;
        if (observed) selected++;
        observed = 0;
        if (!observed) selected += 2;
        check_int(selected, i ? 3 : 2, "volatile bool changes between branches");

        bits.first = i & 1;
        bits.second = i & 2;
        selected = 0;
        if (bits.first) selected++;
        if (!bits.second) selected += 2;
        check_int(selected, (i & 1) + ((i & 2) ? 0 : 2),
                  "bool bitfield branch directions");
    }
}

int main(void)
{
    check_locals();
    check_globals();
    check_casts_and_params();
    check_deref_and_arrays();
    check_conditions();
    check_operators();
    check_compound_ops();
    check_varargs_and_ternary();
    check_short_circuit_phi();
    check_branch_truth_table();
    check_branch_side_effects();
    check_branch_loops();
    check_branch_merges();
    if (failures == 0)
        printf("test tbool completed with great success\n");
    return failures;
}