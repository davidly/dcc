#include <ctype.h>
#include <stdio.h>

struct ScanState {
    char *source;
    long length;
    long cursor;
    int line;
};

static struct ScanState scan_state;
static int helper_calls;
static int mutate_state;
static int failures;

#ifdef WSSCAN_RENAMED_HELPER
#define scan_space whitespace_predicate
#endif

#ifdef WSSCAN_VARIADIC_HELPER
static int scan_space(int value, ...)
#else
static int scan_space(int value)
#endif
{
    ++helper_calls;
    if (mutate_state) {
        scan_state.source = "\nZ";
        scan_state.cursor = 0;
        mutate_state = 0;
    }
    return isspace(value);
}

static void whitespace_scan(void)
{
    while (scan_state.cursor < scan_state.length &&
           scan_space((unsigned char)
                      scan_state.source[scan_state.cursor])) {
#ifdef WSSCAN_EXTRA_BRANCH
        if (scan_state.cursor < 0)
            ++scan_state.line;
#endif
        if (scan_state.source[scan_state.cursor] == '\n')
            ++scan_state.line;
        ++scan_state.cursor;
    }
}

static void run_case(
    char *source, long length, long cursor, int line, int mutate,
    long expected_cursor, int expected_line, int expected_calls)
{
    scan_state.source = source;
    scan_state.length = length;
    scan_state.cursor = cursor;
    scan_state.line = line;
    helper_calls = 0;
    mutate_state = mutate;
    whitespace_scan();
    if (scan_state.cursor != expected_cursor ||
        scan_state.line != expected_line ||
        helper_calls != expected_calls) {
        ++failures;
        printf("case failed cursor=%ld/%ld line=%d/%d calls=%d/%d\n",
               scan_state.cursor, expected_cursor,
               scan_state.line, expected_line,
               helper_calls, expected_calls);
    }
}

int main(void)
{
    run_case(" \t\nX", 4, 0, 10, 0, 3, 11, 4);
    run_case("  X", 1, 0, 20, 0, 1, 20, 1);
    run_case(" X", 0, 0, 30, 0, 0, 30, 0);
    run_case(" X", 2, 0, 40, 1, 1, 41, 2);
    printf("whitespace scan cursor=%ld line=%d calls=%d failures=%d\n",
           scan_state.cursor, scan_state.line, helper_calls, failures);
    return failures != 0;
}
