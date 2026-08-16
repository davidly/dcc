#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

void test_do_while_behavior(void);
static int failures;

int main(void) {
    test_do_while_behavior();
    printf("GDO failures=%d\n", failures);
    return failures != 0;
}

void check(long f)
{
    if (f != 1L) {
        failures++;
        printf("CHECK ABI FAIL %ld\n", f);
    }
}

void test_do_while_behavior(void) {
    /* C89 Rule: All variable declarations must be at the very top of the function */
    int execution_count;
    int condition_variable;
    int continue_hit_count;

    /* =========================================================================
     * CORE BEHAVIOR TESTS
     * ========================================================================= */

    /* TEST 1: Guarantees at least one execution even if condition is false */
    execution_count = 0;
    condition_variable = 0;
    do {
        execution_count++;
    } while (condition_variable != 0);
    check(execution_count == 1);

    /* TEST 2: Standard multi-iteration loop behavior */
    execution_count = 0;
    condition_variable = 5;
    do {
        execution_count++;
        condition_variable--;
    } while (condition_variable > 0);
    check(execution_count == 5);
    check(condition_variable == 0);


    /* =========================================================================
     * EXPANDED TESTS: BREAK & CONTINUE
     * ========================================================================= */

    /* TEST 3: 'break' statement terminates the loop immediately */
    execution_count = 0;
    condition_variable = 10;
    do {
        execution_count++;
        condition_variable--;
        /* Prematurely exit the loop on the 3rd iteration */
        if (execution_count == 3) {
            break;
        }
    } while (condition_variable > 0);

    /* check loop stopped immediately; condition_variable never reached 0 */
    check(execution_count == 3);
    check(condition_variable == 7);


    /* TEST 4: 'continue' skips the rest of the body but STILL evaluates condition */
    execution_count = 0;
    condition_variable = 5;
    continue_hit_count = 0;
    do {
        execution_count++;
        condition_variable--;

        /* Skip the rest of the loop body for even execution counts */
        if (execution_count % 2 == 0) {
            continue;
        }

        /* This line is skipped whenever 'continue' triggers */
        continue_hit_count++;
    } while (condition_variable > 0);

    /* check total loop ran 5 times, but skipped the tail code on iterations 2 and 4 */
    check(execution_count == 5);
    check(condition_variable == 0);
    check(continue_hit_count == 3);
}
