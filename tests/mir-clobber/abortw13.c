#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#ifdef AW27_VOLATILE_FAILURES
static volatile int failures;
#elif defined(AW13_WIDE_FAILURES)
static long failures;
#else
static int failures;
#endif

#ifdef AW27_ALT_STRINGS
#define OLD_NAME "A27OLD.TMP"
#define NEW_NAME "A27NEW.TMP"
#define FILE_CONTENT "wave27"
#define SUCCESS_TEXT "abort wave27 ok\n"
#else
#define OLD_NAME "AW13OLD.TMP"
#define NEW_NAME "AW13NEW.TMP"
#define FILE_CONTENT "wave13"
#define SUCCESS_TEXT "abort wave13 ok\n"
#endif

#ifdef AW27_LARGE_BUFFER
#define MAIN_BUFFER_SIZE 9
#else
#define MAIN_BUFFER_SIZE 8
#endif

static int open_calls;
static int puts_calls;
static int close_calls;
static int rename_calls;
static int gets_calls;
static int compare_calls;
static int remove_calls;
static int check_calls;
static int print_calls;
static int printable_calls;
static int space_calls;
static int file_oracle;
static int branch_guard;

FILE *w13openb(const char *name, const char *mode)
{
    ++open_calls;
    return fopen(name, mode);
}

#ifdef AW13_FAST_OPEN
extern FILE *__fastcall w13open(const char *name, const char *mode);
#asm
_w13open:
        push    de
        push    hl
        call    _w13openb
        pop     bc
        pop     bc
        ret
#endasm
#else
static FILE *w13open(const char *name, const char *mode)
{
    return w13openb(name, mode);
}
#endif

#ifdef AW13_SWAP_PUTS
static int w13puts(FILE *stream, const char *text)
{
    ++puts_calls;
    return fputs(text, stream);
}
#define W13_PUTS(text, stream) w13puts(stream, text)
#else
static int w13puts(const char *text, FILE *stream)
{
    ++puts_calls;
    return fputs(text, stream);
}
#define W13_PUTS(text, stream) w13puts(text, stream)
#endif

#ifdef AW13_VOID_CLOSE
static void w13close(FILE *stream)
{
    ++close_calls;
    fclose(stream);
}
#else
static int w13close(FILE *stream)
{
    ++close_calls;
    return fclose(stream);
}
#endif

static int w13rename(const char *old_name, const char *new_name)
{
    ++rename_calls;
    return rename(old_name, new_name);
}

static char *w13gets(char *buffer, int size, FILE *stream)
{
    ++gets_calls;
    return fgets(buffer, size, stream);
}

static int w13compare(const char *left, const char *right)
{
    ++compare_calls;
    return strcmp(left, right);
}

static int w13remove(const char *name)
{
    FILE *stream;
    char buffer[8];
    int old_absent;
    int content_ok;
    int removed;
    int new_absent;

    ++remove_calls;
    stream = fopen(OLD_NAME, "r");
    old_absent = stream == NULL;
    if (stream)
        fclose(stream);
    stream = fopen(name, "r");
    content_ok = stream != NULL;
    if (stream) {
        fgets(buffer, sizeof(buffer), stream);
        fclose(stream);
        content_ok = strcmp(buffer, FILE_CONTENT) == 0;
    }
    removed = remove(name);
    stream = fopen(name, "r");
    new_absent = stream == NULL;
    if (stream)
        fclose(stream);
    file_oracle =
        old_absent && content_ok && removed == 0 && new_absent;
    return removed;
}

static int w13printable(int value)
{
    ++printable_calls;
    return isprint(value);
}

static int w13space(int value)
{
    ++space_calls;
    return isspace(value);
}

static void w13check(const char *name, int got, int expected)
{
    ++check_calls;
    if (!!got != !!expected) {
        printf("FAIL %s: got %d expected %d\n", name, got, expected);
        ++failures;
    }
}

static int w13print(const char *format, ...)
{
    va_list arguments;
    int result;

    ++print_calls;
    if (!strcmp(format, SUCCESS_TEXT) ||
        !strcmp(format, "%s"))
        printf(
            "oracle files=%d open=%d puts=%d close=%d rename=%d "
            "gets=%d compare=%d remove=%d check=%d print=%d "
            "printable=%d space=%d failures=%d\n",
            file_oracle, open_calls, puts_calls, close_calls,
            rename_calls, gets_calls, compare_calls, remove_calls,
            check_calls, print_calls, printable_calls, space_calls,
            (int)failures);
    va_start(arguments, format);
    result = vprintf(format, arguments);
    va_end(arguments);
    return result;
}

#ifdef AW13_RETURNING_ABORT
static void w13abort(void)
#else
static _Noreturn void w13abort(void)
#endif
{
    printf("abort-call=1\n");
    abort();
}

int main(void)
{
#ifdef AW27_VOLATILE_FILE
    FILE * volatile file;
#else
    FILE *file;
#endif
    char buffer[MAIN_BUFFER_SIZE];

    file = w13open(OLD_NAME, "w");
    W13_PUTS(FILE_CONTENT, file);
    w13close(file);
    w13check(
        "rename_ret",
        w13rename(OLD_NAME, NEW_NAME), 0);
    file = w13open(NEW_NAME, "r");
#ifdef AW13_BRANCH_GUARD
    if (!file || branch_guard) {
#else
    if (!file) {
#endif
        w13print("FAIL rename: new file not found\n");
        ++failures;
    } else {
        w13gets(buffer, sizeof(buffer), file);
        w13close(file);
        w13check(
            "rename_content",
            w13compare(buffer, FILE_CONTENT), 0);
    }
    file = w13open(OLD_NAME, "r");
    if (file) {
        w13print("FAIL rename: old file still exists\n");
        w13close(file);
        ++failures;
    }
    w13remove(NEW_NAME);
    w13check("isgraph_A", w13printable('A') && !w13space('A'), 1);
    w13check("isgraph_z", w13printable('z') && !w13space('z'), 1);
    w13check("isgraph_0", w13printable('0') && !w13space('0'), 1);
    w13check("isgraph_bang", w13printable('!') && !w13space('!'), 1);
    w13check("isgraph_sp", w13printable(' ') && !w13space(' '), 0);
    w13check("isgraph_tab", w13printable('\t') && !w13space('\t'), 0);
    w13check("isgraph_nul", w13printable('\0') && !w13space('\0'), 0);
    if (failures) {
#ifdef AW13_WIDE_FAILURES
        w13print("abort wave13 FAILED %ld\n", failures);
#else
        w13print("abort wave13 FAILED %d\n", failures);
#endif
#ifdef AW13_RETURN_TWO
        return 2;
#else
        return 1;
#endif
    }
#ifdef AW13_PRINT_PAIR
    w13print("%s", SUCCESS_TEXT);
#else
    w13print(SUCCESS_TEXT);
#endif
    w13abort();
    w13print("FAIL abort: returned\n");
#ifdef AW13_RETURN_TWO
    return 2;
#else
    return 1;
#endif
}
