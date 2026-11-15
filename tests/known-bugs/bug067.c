/* BUG-067: sizeof of a derived expression over a nested-block local.
 *
 * Block-scope declaration spans are replayed into the symbol table after the
 * enclosing statement is lowered. BUG-066 fixed the bare-identifier operand
 * through the declared-object table, but an operand that derives from such a
 * local - an element of a block-scoped VLA, a struct member, or a dereferenced
 * pointer - still types its base identifier as int and reports 2 bytes.
 *
 * Expected (clang, fixed-width types): vla 4 3 / member 4 / deref 4 / mixed 8
 * dcc main 7dbe99d8:                   vla 2 6 / member 2 / deref 2 / mixed 4
 */
#include <stdio.h>
#include <stdint.h>

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
        struct { int32_t q; uint8_t r; } s;
        s.r = 0;
        printf("member %d\n", (int)sizeof s.q);
    }
    {
        int32_t cell = 0;
        int32_t *p = &cell;
        printf("deref %d\n", (int)sizeof *p);
    }
    {
        float f = 0.0f;
        float *pf = &f;
        printf("mixed %d\n", (int)(sizeof *pf + sizeof pf[0]));
    }
    return 0;
}
