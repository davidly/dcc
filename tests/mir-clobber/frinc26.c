#include <stdio.h>

#ifdef FRINC_VOLATILE_RESULTS
#define RESULT_QUAL volatile int
#elif defined(FRINC_UNSIGNED_RESULTS)
#define RESULT_QUAL unsigned int
#elif defined(FRINC_NARROW_RESULTS)
#define RESULT_QUAL signed char
#else
#define RESULT_QUAL int
#endif

#ifdef FRINC_ALT_STRINGS
#define INPUT_TEXT "world"
#define OUTPUT_TEXT "for increment alternate %d,%d,%d,%d,%d,%d,%d\n"
#else
#define INPUT_TEXT "hello"
#define OUTPUT_TEXT "for increment %d,%d,%d,%d,%d,%d,%d\n"
#endif

#ifdef FRINC_CHANGED_VALUES
#define INT_LIMIT 3
#define INT_RESULT 6
#define POINTER_LENGTH 4
#define POINTER_RESULT 3
#define MULTIPLY_LIMIT 4
#define MULTIPLY_RESULT 9
#else
#define INT_LIMIT 4
#define INT_RESULT 10
#define POINTER_LENGTH 5
#define POINTER_RESULT 4
#define MULTIPLY_LIMIT 5
#define MULTIPLY_RESULT 14
#endif

static int sum_prefix_int(int n)
{
    int total = 0;
    int i = 0;
    for (++i; i <= n; ++i)
        total += i;
    return total;
}

static int sum_postfix_int(int n)
{
    int total = 0;
    int i = 0;
    for (i++; i <= n; i++)
        total += i;
    return total;
}

static int walk_prefix_ptr(const char *text, int length)
{
    const char *cursor = text + length;
    int steps = 0;
    for (--cursor; cursor != text; --cursor)
        steps++;
    return steps;
}

static int walk_postfix_ptr(const char *text, int length)
{
    const char *cursor = text + length;
    int steps = 0;
    for (cursor--; cursor != text; cursor--)
        steps++;
    return steps;
}

static int mul_init_int(int n)
{
    int total = 0;
    int i = 1;
    for (i *= 2; i <= n; i++)
        total += i;
    return total;
}

static int deref_compound_init(void)
{
    int value = 10;
    int *pointer = &value;
    int total = 0;
    for (*pointer -= 6; *pointer < 8; (*pointer)++)
        total += *pointer;
    return total;
}

static int index_compound_init(void)
{
    int values[3];
    int total = 0;
    values[1] = 0;
    for (values[1] += 2; values[1] < 6; values[1]++)
        total += values[1];
    return total;
}

int main(void)
{
    RESULT_QUAL a = sum_prefix_int(INT_LIMIT);
    RESULT_QUAL b = sum_postfix_int(INT_LIMIT);
    RESULT_QUAL c = walk_prefix_ptr(INPUT_TEXT, POINTER_LENGTH);
    RESULT_QUAL d = walk_postfix_ptr(INPUT_TEXT, POINTER_LENGTH);
    RESULT_QUAL e = mul_init_int(MULTIPLY_LIMIT);
    RESULT_QUAL f = deref_compound_init();
    RESULT_QUAL g = index_compound_init();

    printf(OUTPUT_TEXT, a, b, c, d, e, f, g);
    return !(a == INT_RESULT && b == INT_RESULT &&
             c == POINTER_RESULT && d == POINTER_RESULT &&
             e == MULTIPLY_RESULT && f == 22 && g == 14);
}
