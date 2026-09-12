/* Dedicated semantic controls for the wide-string runner schedule. */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#ifndef _MSC_VER
#define _countof(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifdef W23_SIGNED_WIDE
typedef int w23_char;
#else
typedef wchar_t w23_char;
#endif

#ifdef W23_SMALL_BUFFER
#define W23_BUFFER_COUNT 4095
#else
#define W23_BUFFER_COUNT 4096
#endif

#ifdef W23_VOLATILE_BUFFER
volatile w23_char wide_buffer[W23_BUFFER_COUNT];
#else
w23_char wide_buffer[W23_BUFFER_COUNT];
#endif

static w23_char *w23_copy(w23_char *dest, const w23_char *src)
{
    w23_char *result = dest;

    while (*src)
        *dest++ = *src++;
    *dest = 0;
    return result;
}

static w23_char *w23_first(const w23_char *str, int c)
{
    while (*str != 0) {
        if (*str == c)
            return (w23_char *)str;
        str++;
    }
    return NULL;
}

static w23_char *w23_last(const w23_char *str, int c)
{
    const w23_char *last_occurrence = NULL;

    while (*str != 0) {
        if (*str == c)
            last_occurrence = str;
        str++;
    }
    return (w23_char *)last_occurrence;
}

static w23_char *w23_find(
    const w23_char *haystack, const w23_char *needle)
{
    if (!*needle)
        return (w23_char *)haystack;
    while (*haystack) {
        const w23_char *h = haystack;
        const w23_char *n = needle;

        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n)
            return (w23_char *)haystack;
        haystack++;
    }
    return NULL;
}

#ifdef W23_LENGTH_EXTRA_ARG
static size_t w23_length(const w23_char *str, int ignored)
#elif defined(W23_LENGTH_LONG)
static unsigned long w23_length(const w23_char *str)
#elif defined(W23_VARIADIC_LENGTH)
static size_t w23_length(const w23_char *str, ...)
#else
static size_t w23_length(const w23_char *str)
#endif
{
    const w23_char *orig = str;

#ifdef W23_LENGTH_EXTRA_ARG
    (void)ignored;
#endif
    while (*str != 0)
        str++;
    return str - orig;
}

static size_t w23_alt_length(const w23_char *str)
{
    const w23_char *orig = str;

    while (*str != 0)
        str++;
    return str - orig;
}

static int w23_compare(const void *left, const void *right, size_t count)
{
    return memcmp(left, right, count);
}

#ifdef W23_INSERT_CALL
static void w23_touch(void)
{
}
#endif

#ifdef W23_RENAMED_LOCALS
#define i iteration
#define start begin_index
#define end end_index
#define len span
#define orig saved_character
#define slen measured_length
#define pbang found_character
#define alpha alphabet
#define offset pattern_offset
#define pattern sought_pattern
#define p found_pattern
#define l printed_length
#endif

#ifdef W23_RETURN_INT
static int probe_wide(void)
#else
static void probe_wide(void)
#endif
{
#ifdef W23_SHARED_CHAR_ORIG
    char orig;
#endif
#ifdef W23_ALT_FILL_BOUND
    for (int i = 0; i != _countof(wide_buffer); i++)
#else
    for (int i = 0; i < _countof(wide_buffer); i++)
#endif
#ifdef W23_REORDER_FILL
        wide_buffer[i] = ((i % 26) + L'a');
#else
        wide_buffer[i] = (L'a' + (i % 26));
#endif

#ifdef W23_INSERT_CALL
    w23_touch();
#endif
    printf("testing wcslen\n");
    for (int i = 0; i < 1000; i++) {
        int start = ((unsigned int)rand() % 300);
        int end = 1 + start + ((unsigned int)rand() % 3000);
        int len = end - start;
#ifdef W23_SHARED_CHAR_ORIG
        orig = wide_buffer[end];
#else
        w23_char orig = wide_buffer[end];
#endif
        wide_buffer[end] = 0;
#ifdef W23_ALT_LENGTH
        int slen = w23_alt_length(wide_buffer + start);
#else
#ifdef W23_LENGTH_EXTRA_ARG
        int slen = w23_length(wide_buffer + start, 0);
#else
        int slen = w23_length(wide_buffer + start);
#endif
#endif
        if (len != slen) {
            printf("wcslen failed: iteration %d, len %d, wcslen %d, start %d, end %d\n", i, len, slen, start, end);
            exit(1);
        }
        wide_buffer[end] = orig;
    }

    printf("testing wcschr and wcsrchr\n");
    for (int i = 0; i < 1000; i++) {
        int start = ((unsigned int)rand() % 300);
        int end = 1 + start + ((unsigned int)rand() % 70);
        int len = end - start;
#ifdef W23_SHARED_CHAR_ORIG
        orig = wide_buffer[end];
#else
        w23_char orig = wide_buffer[end];
#endif
        wide_buffer[end] = L'!';
        w23_char *pbang = w23_first(wide_buffer + start, L'!');
        if (!pbang) {
            printf("wcschr failed to find char: iteration %d, len %d, start %d, end %d\n", i, len, start, end);
            exit(1);
        }
        if (pbang != (wide_buffer + end)) {
            printf("wcschr offset incorrect: iteration %d, len %d, start %d, end %d\n", i, len, start, end);
            exit(1);
        }
        pbang = w23_last(wide_buffer + start, L'!');
        if (!pbang) {
            printf("wcsrchr failed to find char: iteration %d, len %d, start %d, end %d\n", i, len, start, end);
            exit(1);
        }
        if (pbang != (wide_buffer + end)) {
            printf("wcsrchr offset incorrect: iteration %d, len %d, start %d, end %d\n", i, len, start, end);
            exit(1);
        }
        wide_buffer[end] = orig;
    }

    printf("testing wcsstr\n");
#ifdef W23_ALIAS_ALPHA
    w23_char *alpha = wide_buffer;
#elif defined(W23_STATIC_ALPHA)
    static w23_char alpha[27];
#else
    w23_char alpha[27];
#endif
    w23_copy(alpha, L"abcdefghijklmnopqrstuvwxyz");
    for (int i = 0; i < 1000; i++) {
        int start = ((unsigned int)rand() % 300);
        int offset = ((unsigned int)rand() % 26);
        int len = 1 + ((unsigned int)rand() % (26 - offset));
        if ((offset + len) > 26) {
            printf("test bug offset %d, len %d\n", offset, len);
            exit(1);
        }
        const w23_char *pattern = alpha + offset;
        const w23_char *p = w23_find(wide_buffer + start, pattern);
        if (!p) {
            printf("wcsstr pattern not found iteration %d, start %d, offset %d, len %d, pattern %ls\n", i, start, offset, len, pattern);
            exit(1);
        }
#ifdef W23_ALT_COMPARE
        if (w23_compare(p, pattern, len * sizeof(w23_char)))
#elif defined(W23_SWAP_COMPARE_ARGS)
        if (memcmp(pattern, p, len * sizeof(w23_char)))
#else
        if (memcmp(p, pattern, len * sizeof(w23_char)))
#endif
        {
            printf("wcsstr found the wrong pattern iteration %d, start %d, offset %d, len %d, pattern %ls\n", i, start, offset, len, pattern);
            exit(1);
        }
    }

    printf("testing printf with wide strings\n");
    for (int i = 0; i < 20; i++) {
        int start = (i * 37) % 300;
        int len = 1 + (i * 17) % 70;
        int end = start + len;
#ifdef W23_SHARED_CHAR_ORIG
        orig = wide_buffer[end];
#else
        char orig = wide_buffer[end];
#endif
        wide_buffer[end] = 0;
#ifdef W23_ALT_LENGTH
        int l = w23_alt_length(wide_buffer + start);
#else
#ifdef W23_LENGTH_EXTRA_ARG
        int l = w23_length(wide_buffer + start, 0);
#else
        int l = w23_length(wide_buffer + start);
#endif
#endif
        printf("%2d (%2d): %ls\n", len, l, wide_buffer + start);
        wide_buffer[end] = orig;
    }
#ifdef W23_RETURN_INT
    return 0;
#endif
}

#ifdef W23_RENAMED_LOCALS
#undef i
#undef start
#undef end
#undef len
#undef orig
#undef slen
#undef pbang
#undef alpha
#undef offset
#undef pattern
#undef p
#undef l
#endif

int main(void)
{
    srand(317);
    probe_wide();
    puts("wstr23 oracle failures=0");
    return 0;
}
