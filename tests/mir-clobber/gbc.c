#include <stdio.h>
#include <string.h>

static char srcbuf[64];
static char outbuf[64];
static int inline_values[16];
static int unsafe_call_count;
static inline int safe_index(int index) { return index * 2; }
static int unsafe_index_step(int index) { unsafe_call_count++; return index; }
static inline int unsafe_index(int index) { return unsafe_index_step(index); }
static unsigned int sum_inline_safe(const int *values, int count)
{
    int i;
    unsigned int total;
    total = 40000U;
    for (i = 0; i < count; i++) total += values[safe_index(i)];
    return total;
}
static int sum_inline_unsafe(const int *values, int count)
{
    int i;
    int total;
    total = 0;
    for (i = 0; i < count; i++) total += values[unsafe_index(i)];
    return total;
}
static long count_long_index(const char *in)
{
    long i; int c; i = 0;
    while (in[i]) { c = (unsigned char)in[i++]; (void)c; }
    return i;
}
static void copy_long_index(const char *in)
{
    long i; long o; int c; i = 0; o = 0;
    while (in[i]) { c = (unsigned char)in[i++]; outbuf[o++] = (char)c; }
    outbuf[o] = 0;
}
static int checks = 0, failures = 0;
static void ck_long(long got, long want, const char *label)
{
    checks++;
    if (got < 0 || got != want) {
        failures++;
        printf("FAIL %s: got %ld want %ld\n", label, got, want);
    }
}
static void ck_str(const char *got, const char *want, const char *label)
{
    checks++;
    if (strcmp(got, want) != 0) { failures++; printf("FAIL %s\n", label); }
}
int main(void)
{
    int i;
    strcpy(srcbuf, "the quick brown fox jumps over the lazy dog");
    ck_long(count_long_index(srcbuf), (long)strlen(srcbuf), "count1");
    copy_long_index(srcbuf); ck_str(outbuf, srcbuf, "copy1");
    strcpy(srcbuf, "abc");
    ck_long(count_long_index(srcbuf), 3, "count2");
    copy_long_index(srcbuf); ck_str(outbuf, "abc", "copy2");
    for (i = 0; i < 16; i++) inline_values[i] = i;
    ck_long(sum_inline_safe(inline_values, 4), (long)40012U, "inline-safe");
    unsafe_call_count = 0;
    ck_long(sum_inline_unsafe(inline_values, 4), 6, "inline-unsafe");
    ck_long(unsafe_call_count, 4, "inline-call-count");
    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
