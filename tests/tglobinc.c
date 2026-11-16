/* tglobinc.c - a global stored to another global plus or minus one
 *
 * `o = g + 1` was taken for `++g`: the in-place increment matcher compared
 * only the storage class and offset of the load and the store, which every
 * external global shares, so it incremented g in place and returned an o
 * that was never written.
 */

#include <stdio.h>

static int fails;
unsigned short g;
unsigned o;
int s;

static void check(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %lx want %lx\n", name, got, want);
        ++fails;
    }
}

/* The unused parameters give each function a frame, which takes it to the
 * matcher. */
unsigned plus_one(unsigned a, unsigned short b, int i, unsigned *p) { o = g + 1; return o; }
unsigned minus_one(unsigned a, unsigned short b, int i, unsigned *p) { o = g - 1; return o; }
unsigned plus_true(unsigned a, unsigned short b, int i, unsigned *p) { { o = (g + (7U <= 7U)); return o; } }
int signed_plus_one(int a, unsigned short b, int i, unsigned *p) { s = a; o = s + 1; return (int)o; }

int main(void)
{
    g = 65534u;
    check("plus_one", plus_one(7, 0, 0, 0), 0xffffL);
    check("minus_one", minus_one(7, 0, 0, 0), 0xfffdL);
    check("plus_true", plus_true(7, 0, 0, 0), 0xffffL);
    check("g_unchanged", g, 0xfffeL);
    check("signed_plus_one", signed_plus_one(41, 0, 0, 0), 42L);
    check("s_unchanged", s, 41L);
    if (fails == 0)
        printf("tglobinc passed\n");
    return fails;
}
