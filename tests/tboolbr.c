/* Branch-only bool loads, short circuits, and snapshot safety. */
#include <stdio.h>
#include <stdbool.h>

static _Bool global_true, global_false;
static int failures;

static void check_int(int got, int want, const char *name)
{
    if (got != want) {
        printf("FAIL %s got=%d want=%d\n", name, got, want);
        failures++;
    }
}

static int branch_on_globals(void)
{
    int result = 0;
    if (global_true) result += 3;
    if (!global_false) result += 5;
    if (global_true && !global_false) result += 7;
    if (global_false || global_true) result += 11;
    return result;
}

static void assign_bool(_Bool *value, int setting)
{
    *value = setting;
}

static void check_branch_snapshots(void)
{
    _Bool saved;
    volatile _Bool observed;
    int hits = 0;

    global_true = 9;
    global_false = 0;
    check_int(branch_on_globals(), 26, "global bool branches true");
    global_true = 0;
    global_false = 8;
    check_int(branch_on_globals(), 11, "global bool branches false");

    saved = global_false;
    global_false = 0;
    if (saved) hits++;
    if (global_false) hits += 10;
    check_int(hits, 1, "branch keeps saved bool snapshot");
    observed = 3;
    if (observed) hits++;
    observed = 0;
    if (observed) hits += 10;
    check_int(hits, 2, "volatile bool branches");
    check_int(saved, 1, "branch and value consumers");
    assign_bool(&saved, 0);
    if (saved) hits += 10;
    assign_bool(&saved, 7);
    if (saved) hits++;
    check_int(hits, 3, "address-taken local bool branches");
    global_true = 1;
}

int main(void)
{
    check_branch_snapshots();
    if (!failures)
        printf("test tboolbr completed with great success\n");
    return failures;
}
