#include <stdio.h>
#include <string.h>

/* Regression test for a real dccpeep bug in pass_elim_dead_ix_stores: a
 * local array/struct based more than 127 bytes from IX (so its OWN base
 * address cannot be expressed as a single (ix+d) displacement) still has
 * interior bytes that land inside the trackable -128..127 window once an
 * earlier pass (pass_ix_addr_byte_store_imm) folds its per-byte
 * initializer stores into direct `ld (ix+d),imm` form. The escape-
 * detection in pass_elim_dead_ix_stores required the escaping address's
 * OWN base offset to fit in -128..127 before flushing pending stores at
 * all - so passing this array's address to a function (here, printf via
 * "%s") was invisible to the pass, and every interior byte after the
 * first two looked "never read" at the function's `ret` and was wrongly
 * deleted, corrupting the array to just its first two bytes. Needs a
 * second, unrelated local array/call in between (matching how this was
 * found) so the whole array does not fit within the direct ix-offset
 * window. */

int main(void)
{
    char a[64] = "hello";
    char b[64] = "goodbye";

    strcat(a, " world");
    printf("%s\n", a);
    printf("%s\n", b);

    return 0;
}
