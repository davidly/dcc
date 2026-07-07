/* tc89size.c - C89 sizeof expression tests for dcc */

#include <stdio.h>

struct SzOne {
    char c;
    int i;
    long l;
};

static int fails;
static int ga[5];
static long gl[3];
static struct SzOne gs[2];
static char gstr[] = "abcd";

static void chki(const char *name, int got, int expect)
{
    if (got != expect) {
        printf("FAIL %s got %d expected %d\n", name, got, expect);
        fails = fails + 1;
    }
}

/* Arrays declared in a NESTED scope (a bare block, an if/for body, or several
 * levels deep) enter the local symbol table only when their declaration is
 * emitted, not when the enclosing block's AST is built.  sizeof of such an
 * array must therefore be resolved at emit time.  Regression for a bug where
 * nested-scope sizeof was computed at AST-build time - before the symbol was
 * in scope - and silently collapsed to sizeof(int) (so the element-count
 * idiom returned 1 instead of the true length).  Expectations use element
 * counts / sizeof(int) so they are int-width independent. */
static int nb_block_count(void)
{
    {
        int a[10];
        return (int)(sizeof a / sizeof a[0]);   /* 10 */
    }
}

static int nb_block_bytes(void)
{
    {
        int a[10];
        return (int)sizeof a;                   /* 10 * sizeof(int) */
    }
}

static int nb_if_count(int n)
{
    if (n > 0) {
        int a[7];
        return (int)(sizeof a / sizeof a[0]);   /* 7 */
    }
    return -1;
}

static int nb_loop_count(void)
{
    int i;
    int last = 0;
    for (i = 0; i < 3; i++) {
        int a[6];
        last = (int)(sizeof a / sizeof a[0]);   /* 6 */
    }
    return last;
}

static int nb_deep_count(void)
{
    {
        {
            {
                int a[4];
                return (int)(sizeof a / sizeof a[0]);   /* 4 */
            }
        }
    }
}

static int nb_2d_rows(void)
{
    {
        int a[5][3];
        return (int)(sizeof a / sizeof a[0]);   /* 5 rows */
    }
}

static int nb_2d_row_bytes(void)
{
    {
        int a[5][3];
        return (int)sizeof a[0];                /* 3 * sizeof(int) */
    }
}

static int nb_char_bytes(void)
{
    {
        char a[9];
        return (int)sizeof a;                   /* 9 */
    }
}

static int nb_between_locals(void)
{
    {
        int guard = 1;
        int a[8];
        int tail = 2;
        int r = (int)(sizeof a / sizeof a[0]);  /* 8 */
        return r + guard - tail;                /* 8 + 1 - 2 = 7 */
    }
}

static int nb_shadow_inner(void)
{
    int a[3];
    {
        int a[6];
        return (int)(sizeof a / sizeof a[0]);   /* inner array */
    }
}

static int nb_shadow_outer_after(void)
{
    int a[5];
    {
        int a[2];
        (void)a;
    }
    return (int)(sizeof a / sizeof a[0]);       /* outer array */
}

int main(void)
{
    struct SzOne s;
    int *ip;
    long *lp;
    struct SzOne *sp;
    char *cp;

    ip = ga;
    lp = gl;
    sp = gs;
    cp = gstr;
    fails = 0;

    chki("sizeof_char", sizeof(char), 1);
    chki("sizeof_int", sizeof(int), 2);
    chki("sizeof_long", sizeof(long), 4);
    chki("sizeof_ptr", sizeof(ip), 2);

    chki("sizeof_array", sizeof ga, 10);
    chki("sizeof_array_elem", sizeof ga[0], 2);
    chki("sizeof_star_ip", sizeof *ip, 2);
    chki("sizeof_ip_index", sizeof ip[0], 2);

    chki("sizeof_long_array", sizeof gl, 12);
    chki("sizeof_long_elem", sizeof gl[0], 4);
    chki("sizeof_star_lp", sizeof *lp, 4);
    chki("sizeof_lp_index", sizeof lp[0], 4);

    chki("sizeof_struct", sizeof s, 7);
    chki("sizeof_struct_array", sizeof gs, 14);
    chki("sizeof_struct_elem", sizeof gs[0], 7);
    chki("sizeof_sp_index", sizeof sp[0], 7);
    chki("sizeof_field_i", sizeof s.i, 2);
    chki("sizeof_field_l", sizeof s.l, 4);
    chki("sizeof_arrow_l", sizeof sp->l, 4);

    chki("sizeof_string_lit", sizeof "abc", 4);
    chki("sizeof_char_array", sizeof gstr, 5);
    chki("sizeof_char_elem", sizeof cp[0], 1);

    chki("sizeof_expr_long", sizeof(ga[0] + 1L), 4);
    chki("sizeof_expr_uint", sizeof(ga[0] + 1U), 2);
    chki("sizeof_compare", sizeof(ga[0] < 1L), 2);

    /* sizeof of arrays declared in nested scopes (resolved at emit time). */
    chki("nb_block_count", nb_block_count(), 10);
    chki("nb_block_bytes", nb_block_bytes(), 10 * (int)sizeof(int));
    chki("nb_if_count", nb_if_count(1), 7);
    chki("nb_loop_count", nb_loop_count(), 6);
    chki("nb_deep_count", nb_deep_count(), 4);
    chki("nb_2d_rows", nb_2d_rows(), 5);
    chki("nb_2d_row_bytes", nb_2d_row_bytes(), 3 * (int)sizeof(int));
    chki("nb_char_bytes", nb_char_bytes(), 9);
    chki("nb_between_locals", nb_between_locals(), 7);
    chki("nb_shadow_inner", nb_shadow_inner(), 6);
    chki("nb_shadow_outer_after", nb_shadow_outer_after(), 5);

    if (fails) {
        printf("tc89size failed: %d\n", fails);
        return 1;
    }

    printf("tc89size completed with great success\n");
    return 0;
}
