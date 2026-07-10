/* tsretmem.c - member access on the rvalue result of a struct-returning call.
 *
 * Exercises `f(...).field` where the base is the value returned by a
 * struct-returning function (an aggregate rvalue that must be materialized
 * into a hidden temporary before the member offset is applied). Covers the
 * declaration-initializer, for-loop body, return-operand and nested-call
 * contexts, with a guard array to catch any frame-size mismatch that would
 * let the hidden temporary overlap another local.
 *
 * C99 exact-width types keep the host (clang baseline) and the 16-bit dcc
 * target in agreement.
 */
#include <stdio.h>
#include <stdint.h>

struct Pair {
    int16_t lo;
    int16_t hi;
};

static struct Pair make_pair(int16_t a, int16_t b)
{
    struct Pair p;

    p.lo = a;
    p.hi = b;
    return p;
}

static int16_t hi_in_return(int16_t a, int16_t b)
{
    return make_pair(a, b).hi;              /* member on rvalue in a return */
}

int main(void)
{
    int16_t guard[3];
    int16_t init_val = make_pair(3, 4).hi;  /* declaration-initializer context */
    int16_t sum;
    int16_t nested;
    int i;

    guard[0] = 11;
    guard[1] = 22;
    guard[2] = 33;

    sum = 0;
    for (i = 0; i < 3; i++)
        sum = (int16_t)(sum + make_pair((int16_t)i, (int16_t)(i + 1)).hi);

    nested = make_pair(make_pair(1, 2).hi, 7).lo;  /* nested struct-return base */

    /* Parenthesized bases build the same member-on-call node; the frame scan
     * must still reserve their hidden temps (regression: under-sized frame). */
    {
        int16_t paren = (make_pair(6, 8)).hi;          /* initializer */
        paren = (int16_t)(paren + ((make_pair(1, 2))).lo); /* double parens */
        nested = (int16_t)(nested + paren);
    }

    printf("tsretmem init=%d ret=%d sum=%d nested=%d guard=%d,%d,%d\n",
           (int)init_val, (int)hi_in_return(5, 9), (int)sum, (int)nested,
           (int)guard[0], (int)guard[1], (int)guard[2]);
    return 0;
}
