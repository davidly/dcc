#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

#ifdef VARJOIN_RENAMED_FUNCTION
#define REPORT_FUNCTION variadic_join_report_renamed
#else
#define REPORT_FUNCTION variadic_join_report
#endif

#ifdef VARJOIN_RENAMED_HELPER
#define JOIN_FUNCTION gather_joined_text
#else
#define JOIN_FUNCTION join_text
#endif

#ifdef VARJOIN_RENAMED_LOCALS
#define BUFFER joined_text
#define LENGTH joined_length
#define COMMAS separator_count
#define INDEX cursor
#else
#define BUFFER buffer
#define LENGTH length
#define COMMAS commas
#define INDEX index
#endif

static char observed[64];
static int observed_length;

static int JOIN_FUNCTION(char *dst, const char *separator, int count, ...)
{
    va_list arguments;
    int32_t position = 0;
    int index;

    va_start(arguments, count);
    for (index = 0; index < count; ++index) {
        const char *item = va_arg(arguments, const char *);

        if (index > 0) {
            strcpy(dst + position, separator);
            position += (int32_t)strlen(separator);
        }
        strcpy(dst + position, item);
        position += (int32_t)strlen(item);
    }
    va_end(arguments);
    strcpy(observed, dst);
    observed_length = (int)position;
    return (int)position;
}

int REPORT_FUNCTION(void)
{
#ifdef VARJOIN_VOLATILE_BUFFER
    volatile char BUFFER[64];
#else
    char BUFFER[64];
#endif
    int LENGTH = JOIN_FUNCTION(
        BUFFER, ", ", 4, "alpha", "beta", "gamma", "delta");
#ifdef VARJOIN_VOLATILE_COUNT
    volatile int COMMAS = 0;
#else
    int COMMAS = 0;
#endif
    int INDEX;

    for (INDEX = 0; BUFFER[INDEX]; ++INDEX) {
        if (BUFFER[INDEX] == ',')
            ++COMMAS;
    }

#ifdef VARJOIN_ALTERNATE_FLOW
    if (COMMAS < 0)
        return 1;
#endif
    printf(
        "variadic join len=%d commas=%d str=%s\n",
        LENGTH, COMMAS, BUFFER);
    return 0;
}

int main(void)
{
    static const char expected[] = "alpha, beta, gamma, delta";
    static const char *items[] = { "alpha", "beta", "gamma", "delta" };
    int expected_length = 3 * (int)strlen(", ");
    int observed_commas = 0;
    int failures = 0;
    int index;

    REPORT_FUNCTION();
    for (index = 0; index < 4; ++index)
        expected_length += (int)strlen(items[index]);
    for (index = 0; observed[index] != '\0'; ++index)
        if (observed[index] == ',')
            ++observed_commas;
    if (observed_length != expected_length)
        ++failures;
    if (observed_commas != 3)
        ++failures;
    if (strcmp(observed, expected) != 0)
        ++failures;
    printf(
        "variadic join failures=%d observed-length=%d expected-length=%d\n",
        failures, observed_length, expected_length);
    return failures;
}
