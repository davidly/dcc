#include <stdbool.h>
#include <stdio.h>

static unsigned check_calls;
static unsigned check_hash;
static int check_failures;

#ifdef DW21_CHECK_INT_RETURN
static int check(bool condition)
#elif defined(DW21_FASTCALL_CHECK)
void check_body(bool condition)
#elif defined(DW21_VARIADIC_CHECK)
static void check(bool condition, ...)
#else
static void check(bool condition)
#endif
{
    ++check_calls;
    check_hash = check_hash * 3u + (condition ? 1u : 0u);
    if (!condition)
        ++check_failures;
#ifdef DW21_CHECK_INT_RETURN
    return 0;
#endif
}

#ifdef DW21_FASTCALL_CHECK
extern void __fastcall check(bool condition);
#asm
_check:
        push    hl
        call    _check_body
        pop     bc
        ret
#endasm
#endif

#ifdef DW21_ALT_TARGET
static void check_alt(bool condition)
{
    check(condition);
}
#endif

#ifdef DW21_RETURN_VALUE
static int probe_do_while(void)
#else
static void probe_do_while(void)
#endif
{
#ifdef DW21_ARRAY_STATE
    int state[3];
#define execution_count state[0]
#define condition_variable state[1]
#define continue_hit_count state[2]
#elif defined(DW21_UNSIGNED_LOCALS)
    unsigned int execution_count;
    unsigned int condition_variable;
    unsigned int continue_hit_count;
#elif defined(DW21_VOLATILE_LOCALS)
    volatile int execution_count;
    volatile int condition_variable;
    volatile int continue_hit_count;
#elif defined(DW21_STATIC_LOCALS)
    static int execution_count;
    static int condition_variable;
    static int continue_hit_count;
#elif defined(DW21_ALIAS_HIT)
    int execution_count;
    int condition_variable;
#define continue_hit_count execution_count
#else
    int execution_count;
    int condition_variable;
    int continue_hit_count;
#endif

    execution_count = 0;
    condition_variable = 0;
    do {
        execution_count++;
    } while (condition_variable != 0);
    check(execution_count == 1);

    execution_count = 0;
#ifdef DW21_ALT_COUNTS
    condition_variable = 7;
#else
    condition_variable = 5;
#endif
    do {
        execution_count++;
        condition_variable--;
#ifdef DW21_ALT_TERMINATION
    } while (condition_variable != 0);
#else
    } while (condition_variable > 0);
#endif
#ifdef DW21_ALT_COUNTS
    check(execution_count == 7);
#else
    check(execution_count == 5);
#endif
    check(condition_variable == 0);

    execution_count = 0;
#ifdef DW21_ALT_COUNTS
    condition_variable = 12;
#else
    condition_variable = 10;
#endif
    do {
        execution_count++;
        condition_variable--;
#ifdef DW21_ALT_COUNTS
        if (execution_count == 4) {
#else
        if (execution_count == 3) {
#endif
            break;
        }
    } while (condition_variable > 0);
#ifdef DW21_ALT_COUNTS
    check(execution_count == 4);
    check(condition_variable == 8);
#else
    check(execution_count == 3);
    check(condition_variable == 7);
#endif

    execution_count = 0;
#ifdef DW21_ALT_COUNTS
    condition_variable = 7;
#else
    condition_variable = 5;
#endif
    continue_hit_count = 0;
    do {
        execution_count++;
        condition_variable--;
        if (execution_count % 2 == 0) {
            continue;
        }
        continue_hit_count++;
#ifdef DW21_ALT_TERMINATION
    } while (condition_variable != 0);
#else
    } while (condition_variable > 0);
#endif
#ifdef DW21_ALT_COUNTS
    check(execution_count == 7);
#else
    check(execution_count == 5);
#endif
    check(condition_variable == 0);
#ifdef DW21_ALT_TARGET
    check_alt(continue_hit_count == 3);
#elif defined(DW21_ALT_COUNTS)
    check(continue_hit_count == 4);
#else
    check(continue_hit_count == 3);
#endif
#ifdef DW21_RETURN_VALUE
    return 7;
#endif
#ifdef DW21_ARRAY_STATE
#undef execution_count
#undef condition_variable
#undef continue_hit_count
#elif defined(DW21_ALIAS_HIT)
#undef continue_hit_count
#endif
}

int main(void)
{
#ifdef DW21_RETURN_VALUE
    int result = probe_do_while();
#else
    probe_do_while();
#endif
    printf("dw5 oracle calls=%u hash=%u failures=%d\n",
        check_calls, check_hash, check_failures);
    return check_failures != 0 || check_calls != 8 ||
        check_hash != 3280u
#ifdef DW21_RETURN_VALUE
        || result != 7
#endif
        ;
}
