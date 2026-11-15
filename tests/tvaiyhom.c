/* A variadic function whose loop counter earns an IY register home must
 * keep its va_list local in step with the shifted frame: the saved caller
 * IY lives at the slot the va_list would otherwise occupy, and the caller
 * holds a loop-carried value in IY across the call. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

static char observed[64];
static int observed_length;

static int join_text(char *dst, const char *separator, int count, ...)
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
        if ((index & 1) != 0)
            observed[2 + (index & 7)] = (char)('0' + (index & 7));
        if ((index ^ count) == 3)
            observed[0] = (char)('a' + (index % 5));
        if (index * 2 > count + index)
            observed[1] = (char)index;
    }
    va_end(arguments);
    strcpy(observed, dst);
    observed_length = (int)position;
    return (int)position;
}

static int report(void)
{
    char buffer[64];
    int length = join_text(buffer, ", ", 4, "alpha", "beta", "gamma", "delta");
    int commas = 0;
    int index;

    for (index = 0; buffer[index]; ++index) {
        if (buffer[index] == ',')
            ++commas;
    }
    printf("variadic join len=%d commas=%d str=%s\n", length, commas, buffer);
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

    report();
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
    printf("variadic join failures=%d observed-length=%d expected-length=%d\n",
           failures, observed_length, expected_length);
    return failures;
}
