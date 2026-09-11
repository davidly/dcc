/* Dedicated semantic controls for the cast/logical runner schedule. */

#include <stdio.h>

#ifdef CAST26_SIGNED_OR
typedef signed char cast26_or_type;
#else
typedef unsigned char cast26_or_type;
#endif

#ifdef CAST26_UNSIGNED_AND
typedef unsigned char cast26_and_type;
#else
typedef signed char cast26_and_type;
#endif

#ifdef CAST26_RENAMED_LOCALS
#define ors any_values
#define ands all_values
#define nested combined_value
#define i position
#endif

#ifdef CAST26_VOLATILE_OR
#define CAST26_OR_QUALIFIER volatile
#else
#define CAST26_OR_QUALIFIER
#endif

#ifdef CAST26_LARGE_OR
#define CAST26_OR_COUNT 7
#else
#define CAST26_OR_COUNT 6
#endif

#ifdef CAST26_FIXED_REPORT
static int cast26_report(
    const char *format,
    int o0, int o1, int o2, int o3, int o4, int o5,
    int a0, int a1, int a2, int a3, int a4, int n)
{
    return printf(
        format, o0, o1, o2, o3, o4, o5,
        a0, a1, a2, a3, a4, n);
}
#define CAST26_REPORT cast26_report
#else
#define CAST26_REPORT printf
#endif

int main(void)
{
#ifdef CAST26_STATIC_OR
    static cast26_or_type ors[CAST26_OR_COUNT];
#else
    CAST26_OR_QUALIFIER cast26_or_type ors[CAST26_OR_COUNT];
#endif
    cast26_and_type ands[5];
    unsigned char nested;
    int i;

    for (i = 0; i < 6; i++)
#ifdef CAST26_OR_OPERATOR
        ors[i] = (cast26_or_type)((i == 2) || (i == 3));
#else
        ors[i] = (cast26_or_type)((i == 2) || (i == 4));
#endif
    for (i = 0; i < 5; i++)
#ifdef CAST26_AND_OPERATOR
        ands[i] = (cast26_and_type)((i >= 0) && (i < 4));
#else
        ands[i] = (cast26_and_type)((i > 0) && (i < 4));
#endif
#ifdef CAST26_NESTED_DATAFLOW
    nested = (unsigned char)(
        (ors[2] && ands[3]) && !ors[1]);
#else
    nested = (unsigned char)(
        ((ors[2] && ands[3]) || ors[0]) && !ors[1]);
#endif

    CAST26_REPORT(
        "cast26 or=%d%d%d%d%d%d and=%d%d%d%d%d nested=%d\n",
        ors[0], ors[1], ors[2], ors[3], ors[4], ors[5],
        ands[0], ands[1], ands[2], ands[3], ands[4], nested);
#ifdef CAST26_RETURN_ONE
    return 1;
#else
    return 0;
#endif
}
