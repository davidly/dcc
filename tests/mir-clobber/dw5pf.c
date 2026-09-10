#include <stdbool.h>
#include <stdio.h>

static unsigned check_calls;
static unsigned check_hash;
static int check_failures;

static void check(bool condition)
{
    ++check_calls;
    check_hash = check_hash * 3u + (condition ? 1u : 0u);
    if (!condition)
        ++check_failures;
}

static void probe_do_while(void)
{
    int execution_count;
    int condition_variable;
    int continue_hit_count;

    execution_count = 0;
    condition_variable = 0;
    do {
        execution_count++;
    } while (condition_variable != 0);
    check(execution_count == 1);

    execution_count = 0;
    condition_variable = 5;
    do {
        execution_count++;
        condition_variable--;
    } while (condition_variable > 0);
    check(execution_count == 5);
    check(condition_variable == 0);

    execution_count = 0;
    condition_variable = 10;
    do {
        execution_count++;
        condition_variable--;
        if (execution_count == 3) {
            break;
        }
    } while (condition_variable > 0);
    check(execution_count == 3);
    check(condition_variable == 7);

    execution_count = 0;
    condition_variable = 5;
    continue_hit_count = 0;
    do {
        execution_count++;
        condition_variable--;
        if (execution_count % 2 == 0) {
            continue;
        }
        continue_hit_count++;
    } while (condition_variable > 0);
    check(execution_count == 5);
    check(condition_variable == 0);
    check(continue_hit_count == 3);
}

int main(void)
{
    probe_do_while();
    printf("dw5 oracle calls=%u hash=%u failures=%d\n",
        check_calls, check_hash, check_failures);
    return check_failures != 0 || check_calls != 8 ||
        check_hash != 3280u;
}
