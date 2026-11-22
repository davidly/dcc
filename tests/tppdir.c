/* Preprocessor directives the probe of malformed and misused directives
 * found mishandled:
 *   - #if did not expand function-like macros, so VER(1, 2) was 0;
 *   - #if compared -1 > 0u as signed;
 *   - F() for a one-parameter macro was rejected instead of passing one
 *     empty argument, and inside a function compiled as 0;
 *   - an eighth macro parameter was dropped;
 *   - #include NAME, with NAME a macro, was rejected;
 *   - a block comment running over lines in a #define cut the definition;
 *   - _Pragma, a GNU linemarker and #line 0 were not accepted. */
#include <stdio.h>

#define VER(major, minor) ((major) * 100 + (minor))
#define HEADER "tppdir.h"
#include HEADER

#define EMPTY_OK(a) (a 7)
#define EIGHT(a, b, c, d, e, f, g, h) (a + b + c + d + e + f + g + h)
#define SPLIT 1 /* this comment
                   runs over lines */ + 2
#define IS_SET defined(TPPDIR_HEADER_VALUE)

_Pragma("dcc_unknown_pragma")

static int failures;

static void check(const char *what, long got, long want)
{
    if (got != want) {
        printf("%s: got %ld, want %ld\n", what, got, want);
        ++failures;
    }
}

int main(void)
{
#if VER(1, 2) == 102
    check("function-like macro in #if", 1, 1);
#else
    check("function-like macro in #if", 0, 1);
#endif
#if -1 > 0u
    check("unsigned #if", 1, 1);
#else
    check("unsigned #if", 0, 1);
#endif
#if (0, 1) && IS_SET
    check("comma and defined via macro", 1, 1);
#else
    check("comma and defined via macro", 0, 1);
#endif
#if 0 && (1 / 0)
    check("unevaluated division", 0, 1);
#endif
    check("empty argument", EMPTY_OK(), 7);
    check("eight parameters", EIGHT(1, 2, 3, 4, 5, 6, 7, 8), 36);
    check("macro include", TPPDIR_HEADER_VALUE, 42);
    check("comment in #define", SPLIT, 3);
    check("line after comment", __LINE__, 59);
# 500 "tppdir.c"
    check("linemarker", __LINE__, 500);
#line 0
    check("#line 0", __LINE__, 0);
    if (failures == 0)
        printf("tppdir passed\n");
    return 0;
}
