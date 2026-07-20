#include <stdio.h>
#include <string.h>

static int g_failed = 0;

static void verify_int(int actual, int expected, const char *test_name) {
    if (actual == expected) {
        printf("PASS: %s\n", test_name);
    } else {
        printf("FAIL: %s\n", test_name);
        g_failed++;
    }
}

static void verify_str(const char *actual, const char *expected, const char *test_name) {
    if (strcmp(actual, expected) == 0) {
        printf("PASS: %s\n", test_name);
    } else {
        printf("FAIL: %s\n", test_name);
        g_failed++;
    }
}

/* Token pasting and function-like macro isolation. */
#define GLUE(a, b) a ## b
#define SUB_MACRO_VAL() 100
#define EVALUATE_TEST(x) GLUE(PREFIX_, x)

/* Stringification and argument pre-expansion. */
#define TEST_VALUE 99
#define STR_IMMEDIATE(x) #x
#define STR_DEFERRED(x) STR_IMMEDIATE(x)

/* Function-like macro expansion through object macros and macro arguments. */
#define ADD1(x) ((x)+1)
#define VALUE_FROM_CALL ADD1(4)
#define STR_DEFERRED_CALL(x) STR_IMMEDIATE(x)

/* Pasted tokens must be rescanned as normal preprocessing tokens. */
#define VALUE_7 123
#define MAKE_VALUE(n) GLUE(VALUE_, n)

/* Conditional expression handling. */
#define PP_A 3
#define PP_B 4
#if defined(PP_A) && !defined(PP_MISSING) && ((PP_A + PP_B * 2) == 11)
#define CONDITIONAL_VALUE 456
#else
#define CONDITIONAL_VALUE -1
#endif

#undef PP_B
#ifdef PP_B
#define UNDEF_VALUE -1
#elif defined(PP_A)
#define UNDEF_VALUE 789
#else
#define UNDEF_VALUE -2
#endif

/* Regression test: two raw-line scanners run before/around real
   tokenization (the #include-splicing pass and the #if/#ifdef dead-code
   filter pass) used to have no notion of still being inside an
   already-open block comment, so a line that merely started with a
   directive-like word here - after leading whitespace, exactly as a real
   directive would look - was misparsed as one. Every '#' line below is
   comment text, not a directive; if any were mistakenly treated as real,
   this file would fail to compile outright (bad #include target, or a
   spurious #error), or COMMENT_MACRO_SHOULD_NOT_EXIST would leak into
   scope below.
   #include this has no filename after it, just like the original crash
   #include "totally-not-a-real-file-xyz123.h"
   #define COMMENT_MACRO_SHOULD_NOT_EXIST 999
   #if 0
   #error this #error must never be reached
   #endif
   still just a comment. */
#define REAL_MACRO_AFTER_COMMENT 55

/* Empty replacement lists must contribute no preprocessing tokens. */
#define EMPTY_DECL
#define EMPTY_DECL_FN()
EMPTY_DECL static int empty_object_value = 12;
EMPTY_DECL_FN() static int empty_function_value = 34;

/* Integer-valued object macros retain the source literal's base. */
#define OCTAL_CREATE 0100
#define OCTAL_TRUNC 01000

#ifdef COMMENT_MACRO_SHOULD_NOT_EXIST
#define COMMENT_LEAKED 1
#else
#define COMMENT_LEAKED 0
#endif

int main(void) {
    int PREFIX_SUB_MACRO_VAL = 777;

    printf("Running Universal C89 Preprocessor Tests...\n\n");

    verify_int(GLUE(PREFIX_, SUB_MACRO_VAL), 777, "1. Token Pasting (##)");
    verify_str("Part A " "and Part B", "Part A and Part B", "2. String Concatenation");
    verify_int(EVALUATE_TEST(SUB_MACRO_VAL), 777, "3. Macro Isolation during Pasting");
    verify_str(STR_IMMEDIATE(TEST_VALUE), "TEST_VALUE", "4a. Immediate Stringify");
    verify_str(STR_DEFERRED(TEST_VALUE), "99", "4b. Deferred Stringify");

    verify_int(VALUE_FROM_CALL, 5, "5. Object Macro Rescans Function-like Macro");
    verify_str(STR_DEFERRED(ADD1(4)), "((4)+1)", "6. Function-like Macro Argument Pre-expansion");
    verify_int(MAKE_VALUE(7), 123, "7. Pasted Token Rescan");
    verify_int(CONDITIONAL_VALUE, 456, "8. #if defined/expression");
    verify_int(UNDEF_VALUE, 789, "9. #undef and #elif");
    verify_int(COMMENT_LEAKED, 0, "10. Directive-lookalike text inside a comment is not a real directive");
    verify_int(REAL_MACRO_AFTER_COMMENT, 55, "11. Real directive after such a comment still works");
    verify_int(empty_object_value + empty_function_value, 46, "12. Empty macro replacement lists");
    verify_int(OCTAL_CREATE | OCTAL_TRUNC | 1, 577, "13. Octal object macro values");

    printf("\n----------------------------------------\n");
    if (g_failed == 0) {
        printf("ALL COMBINED PREPROCESSOR TESTS PASSED WITH GREAT SUCCESS!\n");
        return 0;
    } else {
        return 1;
    }
}
