#include <stdio.h>
#include <string.h>

static int source_guard_before = 0x1357;
static char source_buffer[64];
static int source_guard_after = 0x2468;
static char output_buffer[64];
static int output_guard_after = 0x3579;

#ifdef LONG_INDEX_CHAR_VALUES
typedef char index_value_t;
#else
typedef int index_value_t;
#endif

#ifdef LONG_INDEX_LOOP_STEP_TWO
#define INLINE_UNSAFE_EXPECTED 2
#else
#define INLINE_UNSAFE_EXPECTED 6
#endif

static index_value_t inline_values[16];
static int inline_guard_after = 0x468a;
static int unsafe_call_count;
static int count_calls;
static int copy_calls;
static int safe_sum_calls;
static int unsafe_sum_calls;
static int string_check_calls;
static int long_check_calls;

static inline int safe_index(int index)
{
    return index * 2;
}

static int unsafe_index_step(int index)
{
    unsafe_call_count++;
    return index;
}

static inline int unsafe_index(int index)
{
    return unsafe_index_step(index);
}

static unsigned int sum_inline_safe(
    const index_value_t *values, int count)
{
    int i;
    unsigned int total;

    safe_sum_calls++;
    total = 40000U;
    for (i = 0; i < count; i++)
        total += values[safe_index(i)];
    return total;
}

static int sum_inline_unsafe(
    const index_value_t *values, int count)
{
    int i;
    int total;

    unsafe_sum_calls++;
    total = 0;
    for (i = 0; i < count; i++)
        total += values[unsafe_index(i)];
    return total;
}

#ifdef LONG_INDEX_UNSIGNED_COUNT
static unsigned long count_long_index(const char *in)
#else
static long count_long_index(const char *in)
#endif
{
    long i;
    int c;

    count_calls++;
    i = 0;
    while (in[i]) {
        c = (unsigned char)in[i++];
        (void)c;
    }
    return i;
}

#ifdef LONG_INDEX_COPY_RETURNS_INT
static int copy_long_index(const char *in)
#else
static void copy_long_index(const char *in)
#endif
{
    long i;
    long o;
    int c;

    copy_calls++;
    i = 0;
    o = 0;
    while (in[i]) {
        c = (unsigned char)in[i++];
        output_buffer[o++] = (char)c;
    }
    output_buffer[o] = 0;
#ifdef LONG_INDEX_COPY_RETURNS_INT
    return 123;
#endif
}

#ifdef LONG_INDEX_SECOND_COPY
static void copy_long_index_alternate(const char *in)
{
    long i;
    long o;
    int c;

    copy_calls++;
    i = 0;
    o = 0;
    while (in[i]) {
        c = (unsigned char)in[i++];
        output_buffer[o++] = (char)c;
    }
    output_buffer[o] = 0;
}
#endif

static int checks;
static int failures;

static void check_guards(void)
{
    if (source_guard_before != 0x1357 ||
        source_guard_after != 0x2468 ||
        output_guard_after != 0x3579 ||
        inline_guard_after != 0x468a) {
        failures++;
        printf("FAIL guard corruption\n");
    }
}

static void check_long(long got, long want, const char *label)
{
    checks++;
    long_check_calls++;
    if (got < 0 || got != want) {
        failures++;
        printf("FAIL %s: got %ld want %ld\n", label, got, want);
    }
    check_guards();
    if (strcmp(label, "inline-call-count") == 0 &&
        (count_calls != 2 || copy_calls != 2 ||
         safe_sum_calls != 1 || unsafe_sum_calls != 1 ||
         string_check_calls != 2 || long_check_calls != 5)) {
        failures++;
        printf("FAIL call identity/ABI counters\n");
    }
}

static void check_string(
    const char *got, const char *want, const char *label)
{
    checks++;
    string_check_calls++;
    if (strcmp(got, want) != 0) {
        failures++;
        printf("FAIL %s\n", label);
    }
    check_guards();
}

int main(void)
{
    int i;

    strcpy(source_buffer, "the quick brown fox jumps over the lazy dog");
    check_long(
        count_long_index(source_buffer),
        (long)strlen(source_buffer), "count1");
    copy_long_index(source_buffer);
    check_string(output_buffer, source_buffer, "copy1");

    strcpy(source_buffer, "abc");
    check_long(count_long_index(source_buffer), 3, "count2");
#ifdef LONG_INDEX_SECOND_COPY
    copy_long_index_alternate(source_buffer);
#else
    copy_long_index(source_buffer);
#endif
    check_string(output_buffer, "abc", "copy2");

    for (i = 0; i < 16;
#ifdef LONG_INDEX_LOOP_STEP_TWO
         i += 2
#else
         i++
#endif
    )
        inline_values[i] = (index_value_t)i;
    check_long(
        sum_inline_safe(inline_values, 4),
        (long)40012U, "inline-safe");
    unsafe_call_count = 0;
    check_long(
        sum_inline_unsafe(inline_values, 4),
        INLINE_UNSAFE_EXPECTED, "inline-unsafe");
    check_long(
        unsafe_call_count, 4, "inline-call-count");

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
#ifdef LONG_INDEX_RETURN_BOOL
    return failures != 0;
#else
    return failures ? 1 : 0;
#endif
}
