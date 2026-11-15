/* sizeof of a derived expression over a nested-block local (BUG-067, #338).
 *
 * Block-scope declaration spans are replayed into the symbol table after the
 * enclosing statement is lowered, so an operand that derives from such a
 * local - an element of a block-scoped VLA, a struct member, a dereferenced
 * pointer, or arithmetic over one - had its base identifier typed as int and
 * reported 2 bytes. The deferred sizeof is now resolved during the
 * statement's metadata walk, after the declarations have been replayed.
 *
 * Host-portable: fixed-width types only, no whole-struct sizes (dcc does not
 * pad), no bare pointer sizes and no int-promoted operands.
 */
#include <stdio.h>
#include <stdint.h>

struct T { int32_t a[3]; uint8_t b; float f; };
int g = 3;

int main(int argc, char **argv)
{
    int n = argc + 2;
    (void)argv;
    {
        int32_t v[n];
        v[0] = 0;
        printf("vla %d %d\n", (int)sizeof v[0], (int)(sizeof v / sizeof v[0]));
    }
    {
        uint8_t m[n][4];
        m[0][0] = 1;
        printf("vla2 %d %d\n", (int)sizeof m[0], (int)(sizeof m / sizeof m[0]));
    }
    {
        struct { int32_t q; uint8_t r; } s;
        s.r = 0;
        printf("member %d\n", (int)sizeof s.q);
    }
    {
        struct T t; struct T *pt = &t; struct T arr[2];
        t.b = 0; pt->b = 0; arr[0].b = 0;
        printf("marr %d %d %d %d %d\n", (int)sizeof t.a, (int)sizeof t.a[0],
               (int)sizeof pt->a, (int)sizeof pt->f, (int)sizeof arr[1].a[2]);
    }
    {
        int32_t cell = 0;
        int32_t *p = &cell;
        printf("deref %d %d %d\n", (int)sizeof *p, (int)sizeof p[0],
               (int)sizeof(*p + g));
    }
    {
        float f = 0.0f;
        float *pf = &f;
        printf("mixed %d %d\n", (int)(sizeof *pf + sizeof pf[0]),
               (int)sizeof(pf[0] * 2));
    }
    {
        int32_t x = 1;
        {
            uint8_t x = 2;
            printf("shadow %d %d\n", (int)sizeof x, (int)sizeof -(int32_t)x);
        }
        printf("outer %d %d\n", (int)sizeof x, (int)sizeof(x + g));
    }
    for (int32_t i = 0; i < 1; ++i) {
        int32_t *pi = &i;
        printf("for %d %d %d\n", (int)sizeof *pi, (int)sizeof pi[0],
               (int)sizeof(*pi + g));
    }
    return 0;
}
