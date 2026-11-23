/* tppreg.c - preprocessor #if expression regression test */

#define PP_SUM 1 + 2
#define PP_SUM_ALIAS PP_SUM
#define PP_ADD_TWO(x) x + 2
#define PP_CALL_ALIAS PP_ADD_TWO
#define PP_RECURSIVE 1 + PP_RECURSIVE

#if 3 * PP_SUM != 5 || 3 * PP_CALL_ALIAS(1) != 5 || PP_RECURSIVE != 1
#error incorrect preprocessing macro replacement
#endif

#if !defined PP_SUM || !defined(PP_SUM_ALIAS) || defined(PP_ABSENT)
#error defined must inspect the unexpanded macro name
#endif

#if 0
#if PP_ADD_TWO(1, 2)
#error inactive directive was evaluated
#endif
#elif 3 * PP_SUM_ALIAS == 5
#include <limits.h>
#endif
#if !defined CHAR_BIT
#error macro precedence selected the wrong include
#endif

#if -1 < 1U || !(1u < -1) || (-3U % 8U) != 5U || (-1U >> 1) == -1
#error unsigned preprocessing arithmetic was evaluated as signed
#endif
#if (1 ? -1 : 0U) < 0 || (-4 >> 1U) != -2 || (0xffffffffU / 2) != 0x7fffffff
#error incorrect preprocessing operand conversions
#endif
#if 0
#elif -1 > 1U
#include <float.h>
#endif
#if !defined FLT_RADIX
#error unsigned arithmetic selected the wrong include
#endif
#if -1 < 1U
#include "tppreg-inactive.h"
#endif

#include <stdio.h>

#define A 3
#define B 5
#define C 0x10
#define D 010
#define E A
#define F E
#define MASK 0xf0

static int failures;

static void cki(const char *name, int got, int exp)
{
    if (got != exp) {
        printf("FAIL %s got %d expected %d\n", name, got, exp);
        failures++;
    }
}

#if 1 + 2 * 3 == 7
#define R_ARITH 1
#else
#define R_ARITH 0
#endif

#if (1 + 2) * 3 == 9
#define R_PAREN 1
#else
#define R_PAREN 0
#endif

#if C >> 2 == 4 && 1 << 4 == 16
#define R_SHIFT 1
#else
#define R_SHIFT 0
#endif

#if D == 8
#define R_OCTAL 1
#else
#define R_OCTAL 0
#endif

#if (MASK & 0x0f) == 0 && (MASK | 0x0f) == 0xff && (0xaa ^ 0xff) == 0x55
#define R_BITOPS 1
#else
#define R_BITOPS 0
#endif

#if ~0 == -1 && -A == -3 && +B == 5
#define R_UNARY 1
#else
#define R_UNARY 0
#endif

#if A < B && B >= 5 && A <= 3 && B > A
#define R_REL 1
#else
#define R_REL 0
#endif

#if defined(A) && !defined(NOT_DEFINED)
#define R_DEFINED 1
#else
#define R_DEFINED 0
#endif

#if F == 3
#define R_NESTED 1
#else
#define R_NESTED 0
#endif

#if 'A' == 65 && '\n' == 10
#define R_CHAR 1
#else
#define R_CHAR 0
#endif

#if '\a' != 7 || '\b' != 8 || '\f' != 12 || '\v' != 11 || \
    '\7' != 7 || '\77' != 63 || '\101' != 65 || '\377' != 255 || \
    '\x7' != 7 || '\x4A' != 74 || '\x4a' != 74 || '\xa' != 10 || '\xff' != 255
#error incorrect preprocessor character escape
#endif

#if 0
#define R_ELIF 0
#elif A * B == 15
#define R_ELIF 1
#else
#define R_ELIF 0
#endif

int main(void)
{
    cki("arith", R_ARITH, 1);
    cki("paren", R_PAREN, 1);
    cki("shift", R_SHIFT, 1);
    cki("octal", R_OCTAL, 1);
    cki("bitops", R_BITOPS, 1);
    cki("unary", R_UNARY, 1);
    cki("rel", R_REL, 1);
    cki("defined", R_DEFINED, 1);
    cki("nested", R_NESTED, 1);
    cki("char", R_CHAR, 1);
    cki("elif", R_ELIF, 1);

    if (failures) {
        printf("tppreg: %d failure(s)\n", failures);
        return 1;
    }

    printf("tppreg: all tests passed\n");
    return 0;
}
