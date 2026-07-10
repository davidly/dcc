/* tcptrarr.c
 *
 * Reading an element of a (const) array of pointers and using it directly as a
 * call argument.  Regression coverage for the dcc AST support fix in
 * ast_index_pointer_array_elem_type:
 *
 *   1. A read-only (const / const-of-const) array of pointers may be indexed
 *      for a read; const no longer disqualifies the pointer-array element form.
 *   2. Any subscript that folds to a plain integer value is accepted, including
 *      an integer literal, an enum constant, an int variable, an enum-typed
 *      variable, and a constant arithmetic expression.
 *
 * The indexed pointer expression must be recognized as a 16-bit pointer-valued
 * call argument (here, the "%s" argument to printf).  The program is accepted
 * by clang -std=c99 -Wall -Wextra -pedantic; its output is the reference.
 */
#include <stdio.h>
#include <string.h>

enum Status { ST_OK, ST_MISSING, ST_BUSY };

static const char *messages[3] = { "ok", "missing", "busy" };
static const char *const cmessages[2] = { "alpha", "beta" };

static int failures;

static void expect(const char *label, const char *got, const char *want)
{
    if (got == NULL || strcmp(got, want) != 0) {
        printf("FAIL %s got=%s want=%s\n", label, got ? got : "(null)", want);
        ++failures;
    }
}

int main(void)
{
    int i = 2;
    enum Status s = ST_MISSING;

    /* literal subscript on a const pointer array */
    printf("status=%s", messages[0]);
    /* enum-constant subscript */
    printf("/%s", messages[ST_MISSING]);
    /* variable subscript */
    printf("/%s", messages[i]);
    /* enum-typed variable subscript */
    printf("/%s", messages[s]);
    /* constant arithmetic subscript */
    printf("/%s", messages[1 + 1]);
    /* const-of-const pointer array */
    printf("/%s\n", cmessages[0]);

    /* Independent value checks so a wrong element is caught even if the
     * concatenated line above still looks plausible. */
    expect("messages[0]", messages[0], "ok");
    expect("messages[ST_MISSING]", messages[ST_MISSING], "missing");
    expect("messages[i]", messages[i], "busy");
    expect("messages[s]", messages[s], "missing");
    expect("messages[1+1]", messages[1 + 1], "busy");
    expect("cmessages[0]", cmessages[0], "alpha");
    expect("cmessages[1]", cmessages[1], "beta");

    if (failures == 0)
        printf("const pointer-array tests passed\n");
    return failures != 0;
}
