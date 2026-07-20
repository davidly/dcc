#include <stdio.h>
#include <stdlib.h>

static unsigned char *slots[24];
static unsigned int sizes[24];
static unsigned int seed;

static void fail(const char *msg)
{
    printf("FAIL %s\n", msg);
    exit(1);
}

static unsigned int rnd(void)
{
    seed = (unsigned int)(seed * 25173U + 13849U);
    return seed;
}

static unsigned char patt(int slot, unsigned int off)
{
    return (unsigned char)((slot * 37 + off * 13 + 91) & 255U);
}

static void fill(unsigned char *p, unsigned int n, int slot)
{
    unsigned int i;

    for (i = 0; i < n; i++)
        p[i] = patt(slot, i);
}

static void check(unsigned char *p, unsigned int n, int slot, const char *msg)
{
    unsigned int i;

    if (p == 0)
        fail("null check pointer");

    for (i = 0; i < n; i++) {
        if (p[i] != patt(slot, i))
            fail(msg);
    }
}

static void zcheck(unsigned char *p, unsigned int n, const char *msg)
{
    unsigned int i;

    if (p == 0)
        fail("null zero pointer");

    for (i = 0; i < n; i++) {
        if (p[i] != 0)
            fail(msg);
    }
}

static void t_zero(void)
{
    unsigned char *p;

    free(0);
    p = (unsigned char *)malloc(0U);
    if (p == 0)
        fail("malloc zero returned null");
    p[0] = 0x5a;
    free(p);
}

static void t_split(void)
{
    unsigned char *a;
    unsigned char *b;
    unsigned char *c;

    a = (unsigned char *)malloc(20U);
    if (a == 0)
        fail("split setup malloc failed");
    fill(a, 20U, 1);
    free(a);

    b = (unsigned char *)malloc(12U);
    c = (unsigned char *)malloc(1U);
    if (b != a)
        fail("split did not reuse block head");
    /* 20 - 12 = 8 spare bytes, enough to split off a 4-byte tail block (2
     * bytes each of header/footer overhead either side of the tail data). */
    if ((unsigned)c != (unsigned)b + 16U)
        fail("split tail payload wrong address");
    c[0] = 0xa5;
    check(b, 12U, 1, "split head contents changed unexpectedly");
    free(b);
    free(c);
}

static void t_nosplit(void)
{
    unsigned char *a;
    unsigned char *b;
    unsigned char *c;
    unsigned char *guard;

    /* Keep a guard block above the block under test so freeing it never trims
     * the heap top; that lets us exercise the no-split reuse path. */
    a = (unsigned char *)malloc(16U);
    guard = (unsigned char *)malloc(7U);
    if (a == 0 || guard == 0)
        fail("nosplit setup malloc failed");
    free(a);

    b = (unsigned char *)malloc(13U);
    if (b != a)
        fail("nosplit did not reuse block head");
    /* 13 rounds up to 14 (allocations are aligned to even sizes); slack is
     * 16-14 = 2, below the minimum useful split (needs 6 spare bytes: 4
     * bytes header+footer overhead plus a 2-byte usable tail), so no tail
     * fragment must be split off; the block must remain a full 16-byte
     * block.  Freeing it (still non-top) and requesting 16 must reuse the
     * exact same address. */
    free(b);
    c = (unsigned char *)malloc(16U);
    if (c != a)
        fail("nosplit unexpectedly created tail fragment");
    free(c);
    free(guard);
}

static void t_forward(void)
{
    unsigned char *a;
    unsigned char *b;
    unsigned char *c;
    unsigned char *p;

    a = (unsigned char *)malloc(1000U);
    b = (unsigned char *)malloc(1000U);
    c = (unsigned char *)malloc(1000U);
    if (a == 0 || b == 0 || c == 0)
        fail("forward setup malloc failed");
    fill(a, 1000U, 2);
    fill(c, 1000U, 3);

    free(b);
    free(c);
    p = (unsigned char *)malloc(2003U);
    if (p != b)
        fail("forward coalesce failed");
    check(a, 1000U, 2, "forward coalesce damaged previous block");
    fill(p, 2003U, 4);
    free(p);
    free(a);
}

static void t_reverse(void)
{
    unsigned char *a;
    unsigned char *b;
    unsigned char *c;
    unsigned char *p;

    a = (unsigned char *)malloc(1000U);
    b = (unsigned char *)malloc(1000U);
    c = (unsigned char *)malloc(1000U);
    if (a == 0 || b == 0 || c == 0)
        fail("reverse setup malloc failed");

    free(a);
    free(b);
    p = (unsigned char *)malloc(2003U);
    if (p != a)
        fail("reverse coalesce failed");
    fill(p, 2003U, 5);
    free(p);
    free(c);
}

static void t_bridge(void)
{
    unsigned char *a;
    unsigned char *b;
    unsigned char *c;
    unsigned char *p;

    a = (unsigned char *)malloc(1000U);
    b = (unsigned char *)malloc(1000U);
    c = (unsigned char *)malloc(1000U);
    if (a == 0 || b == 0 || c == 0)
        fail("bridge setup malloc failed");

    free(a);
    free(c);
    free(b);
    p = (unsigned char *)malloc(3006U);
    if (p != a)
        fail("bridge coalesce failed");
    fill(p, 3006U, 6);
    free(p);
}

static void t_sizes(void)
{
    unsigned char *g;
    unsigned char *a;
    unsigned char *b;
    unsigned char *p;
    unsigned char *q;
    unsigned int base;
    unsigned int need;

    for (base = 8U; base <= 80U; base += 8U) {
        g = (unsigned char *)malloc(5U);
        a = (unsigned char *)malloc(base);
        b = (unsigned char *)malloc(7U);
        if (g == 0 || a == 0 || b == 0)
            fail("size sweep split setup failed");

        free(a);
        /* remainder == 6: the minimum spare that still splits off a usable
         * (2-byte) tail block - 4 bytes of header+footer overhead either
         * side of the tail data. */
        need = base - 6U;
        p = (unsigned char *)malloc(need);
        q = (unsigned char *)malloc(2U);
        if (p != a)
            fail("size sweep split did not reuse head");
        if ((unsigned)q != (unsigned)a + need + 4U)
            fail("size sweep split tail wrong address");
        free(p);
        free(q);
        free(g);
        free(b);

        g = (unsigned char *)malloc(5U);
        a = (unsigned char *)malloc(base);
        b = (unsigned char *)malloc(7U);
        if (g == 0 || a == 0 || b == 0)
            fail("size sweep nosplit setup failed");

        free(a);
        /* remainder == 4: below the split threshold. */
        need = base - 4U;
        p = (unsigned char *)malloc(need);
        q = (unsigned char *)malloc(1U);
        if (p != a)
            fail("size sweep nosplit did not reuse head");
        if ((unsigned)q == (unsigned)a + need + 4U)
            fail("size sweep nosplit incorrectly made tail");
        free(p);
        free(q);
        free(g);
        free(b);
    }
}

static void t_large(void)
{
    unsigned char *p;
    unsigned char *q;
    unsigned char *r;

    p = (unsigned char *)malloc(32768U);
    if (p == 0)
        fail("large malloc32768 failed");
    p[0] = 0x12;
    p[32767U] = 0x34;
    if (p[0] != 0x12 || p[32767U] != 0x34)
        fail("large edge bytes changed");
    free(p);

    q = (unsigned char *)malloc(32U);
    if (q == 0)
        fail("large small split malloc failed");
    q[0] = 0x56;
    q[31U] = 0x78;
    free(q);

    p = (unsigned char *)malloc(32768U);
    if (p == 0)
        fail("large coalesced malloc32768 failed");
    if (p != q)
        fail("large coalesced malloc wrong address");
    free(p);

    q = (unsigned char *)malloc(1U);
    if (q == 0)
        fail("large guard malloc failed");
    r = (unsigned char *)malloc(65000U);
    if (r != 0)
        fail("large wrap malloc accepted impossible request");
    r = (unsigned char *)malloc(65535U);
    if (r != 0)
        fail("large odd-wrap malloc accepted impossible request");
    free(q);
}

static void t_realloc_size_overflow(void)
{
    unsigned char *p;
    unsigned char *r;

    p = (unsigned char *)malloc(32U);
    if (p == 0)
        fail("realloc overflow setup malloc failed");
    fill(p, 32U, 29);

    r = (unsigned char *)realloc(p, 65535U);
    if (r != 0)
        fail("realloc odd-wrap request did not fail");
    check(p, 32U, 29, "realloc odd-wrap freed or damaged old block");
    free(p);
}

static void t_calloc(void)
{
    unsigned char *p;
    unsigned char *q;

    p = (unsigned char *)calloc(37U, 5U);
    if (p == 0)
        fail("calloc returned null");
    zcheck(p, 185U, "calloc did not zero fresh block");
    fill(p, 185U, 7);
    free(p);

    q = (unsigned char *)calloc(185U, 1U);
    if (q == 0)
        fail("calloc reuse returned null");
    if (q != p)
        fail("calloc did not reuse freed block");
    zcheck(q, 185U, "calloc did not zero reused block");
    free(q);
}

static void t_realloc(void)
{
    unsigned char *p;
    unsigned char *q;
    unsigned char *r;
    unsigned char *s;

    p = (unsigned char *)malloc(64U);
    if (p == 0)
        fail("realloc setup malloc failed");
    fill(p, 64U, 8);

    q = (unsigned char *)realloc(p, 120U);
    if (q == 0)
        fail("realloc grow returned null");
    check(q, 64U, 8, "realloc grow did not preserve contents");
    fill(q, 120U, 9);

    r = (unsigned char *)realloc(q, 16U);
    if (r == 0)
        fail("realloc shrink returned null");
    check(r, 16U, 9, "realloc shrink did not preserve contents");

    s = (unsigned char *)realloc(r, 0U);
    if (s != 0)
        fail("realloc zero did not return null");

    p = (unsigned char *)realloc(0, 32U);
    if (p == 0)
        fail("realloc null did not allocate");
    fill(p, 32U, 10);
    free(p);
}

static void t_wrap(void)
{
    unsigned char *p;
    unsigned char *q;

    p = (unsigned char *)malloc(1U);
    if (p == 0)
        fail("wrap guard setup malloc failed");
    q = (unsigned char *)malloc(65000U);
    if (q != 0)
        fail("wrap guard accepted impossible malloc");
    free(p);
}

static void t_recoalesce(void)
{
    unsigned char *a;
    unsigned char *b;
    unsigned char *guard;
    unsigned char *q;
    unsigned char *r;

    /* Lay out A(200) then B(100), then a guard so B is not the heap-top block
     * (which would let realloc grow it in place).  Free A so the following
     * realloc reuses and splits A's block via the allocate-new path. */
    a = (unsigned char *)malloc(200U);
    b = (unsigned char *)malloc(100U);
    guard = (unsigned char *)malloc(8U);
    if (a == 0 || b == 0 || guard == 0)
        fail("recoalesce setup malloc failed");
    fill(b, 100U, 12);
    free(a);

    /* realloc(B,150) reuses A's 200-byte block as 150 used + a 47-byte free
     * fragment, then frees the old B block.  That free must coalesce with the
     * fragment so the reclaimed space is reused, not stranded. */
    q = (unsigned char *)realloc(b, 150U);
    if (q == 0)
        fail("recoalesce realloc failed");
    check(q, 100U, 12, "recoalesce realloc lost contents");

    /* The fragment sits at q + 150 + HDRSIZE + FTRSIZE (past the used part's
     * own header and footer).  If the old-block free coalesced, a 120-byte
     * malloc reuses it at exactly that address; otherwise malloc is forced
     * to extend the heap and returns a higher address. */
    r = (unsigned char *)malloc(120U);
    if (r == 0)
        fail("recoalesce post malloc failed");
    if ((unsigned)r != (unsigned)q + 154U)
        fail("realloc free did not coalesce (heap fragmented)");
    free(q);
    free(r);
    free(guard);
}

static void t_rezero_coalesce(void)
{
    unsigned char *a;
    unsigned char *b;
    unsigned char *c;
    unsigned char *p;

    a = (unsigned char *)malloc(100U);
    b = (unsigned char *)malloc(100U);
    c = (unsigned char *)malloc(100U);
    if (a == 0 || b == 0 || c == 0)
        fail("rezero setup malloc failed");

    /* Free B, then realloc(C,0) frees C.  With coalescing the B and C blocks
     * (and the trailing free space) merge into one region, so a 250-byte malloc
     * reuses B's address.  Without coalescing the two 100-byte holes are too
     * small and malloc returns a different, later block. */
    free(b);
    p = (unsigned char *)realloc(c, 0U);
    if (p != 0)
        fail("realloc zero did not return null");

    p = (unsigned char *)malloc(250U);
    if (p == 0)
        fail("rezero post malloc failed");
    if (p != b)
        fail("realloc(ptr,0) free did not coalesce (heap fragmented)");
    free(p);
    free(a);
}

static void t_stress(void)
{
    int i;
    int idx;
    int op;
    unsigned int n;
    unsigned int old;
    unsigned int keep;
    unsigned char *p;

    seed = 0xACE1U;
    for (i = 0; i < 24; i++) {
        slots[i] = 0;
        sizes[i] = 0;
    }

    for (i = 0; i < 420; i++) {
        idx = (int)(rnd() % 24U);
        if (slots[idx] != 0) {
            check(slots[idx], sizes[idx], idx + 11, "stress contents changed");
            op = (int)(rnd() % 4U);
            if (op == 0) {
                old = sizes[idx];
                n = (rnd() % 220U) + 1U;
                p = (unsigned char *)realloc(slots[idx], n);
                if (p == 0)
                    fail("stress realloc returned null");
                keep = old < n ? old : n;
                check(p, keep, idx + 11, "stress realloc contents changed");
                slots[idx] = p;
                sizes[idx] = n;
                fill(slots[idx], sizes[idx], idx + 11);
            }
            else {
                free(slots[idx]);
                slots[idx] = 0;
                sizes[idx] = 0;
            }
        }
        else {
            n = (rnd() % 220U) + 1U;
            if ((rnd() & 1U) != 0) {
                p = (unsigned char *)calloc(n, 1U);
                if (p == 0)
                    fail("stress calloc returned null");
                zcheck(p, n, "stress calloc not zero");
            }
            else {
                p = (unsigned char *)malloc(n);
                if (p == 0)
                    fail("stress malloc returned null");
            }
            slots[idx] = p;
            sizes[idx] = n;
            fill(slots[idx], sizes[idx], idx + 11);
        }
    }

    for (i = 0; i < 24; i += 2) {
        if (slots[i] != 0) {
            check(slots[i], sizes[i], i + 11, "stress even final changed");
            free(slots[i]);
            slots[i] = 0;
        }
    }
    for (i = 1; i < 24; i += 2) {
        if (slots[i] != 0) {
            check(slots[i], sizes[i], i + 11, "stress odd final changed");
            free(slots[i]);
            slots[i] = 0;
        }
    }

    p = (unsigned char *)malloc(12000U);
    if (p == 0)
        fail("stress final large malloc failed");
    fill(p, 12000U, 35);
    check(p, 12000U, 35, "stress final large changed");
    free(p);
}

static void t_shrink_inplace(void)
{
    unsigned char *p;
    unsigned char *q;

    /* Shrinking must keep the same address (no allocate + copy) and preserve
     * the leading bytes. */
    p = (unsigned char *)malloc(200U);
    if (p == 0)
        fail("shrink setup malloc failed");
    fill(p, 200U, 20);
    q = (unsigned char *)realloc(p, 50U);
    if (q != p)
        fail("shrink-in-place changed the block address");
    check(q, 50U, 20, "shrink-in-place lost contents");
    free(q);
}

static void t_grow_top(void)
{
    unsigned char *p;
    unsigned char *q;

    /* p is the most recent allocation, so it is the heap-top block; growing it
     * must extend it in place and return the same pointer. */
    p = (unsigned char *)malloc(50U);
    if (p == 0)
        fail("grow-top setup malloc failed");
    fill(p, 50U, 21);
    q = (unsigned char *)realloc(p, 180U);
    if (q != p)
        fail("grow-at-top did not grow in place");
    check(q, 50U, 21, "grow-at-top lost contents");
    fill(q, 180U, 22);
    check(q, 180U, 22, "grow-at-top block not fully usable");
    free(q);
}

static void t_grow_next_free(void)
{
    unsigned char *p;
    unsigned char *next;
    unsigned char *guard;
    unsigned char *q;

    p = (unsigned char *)malloc(50U);
    next = (unsigned char *)malloc(100U);
    guard = (unsigned char *)malloc(16U);
    if (p == 0 || next == 0 || guard == 0)
        fail("grow-next setup malloc failed");
    fill(p, 50U, 23);
    free(next);

    q = (unsigned char *)realloc(p, 120U);
    if (q != p)
        fail("grow into next free block did not stay in place");
    check(q, 50U, 23, "grow into next free block lost contents");
    fill(q, 120U, 24);
    check(q, 120U, 24, "grown next-free block not fully usable");
    free(q);
    free(guard);
}

static void t_grow_absorb_nosplit(void)
{
    unsigned char *p;
    unsigned char *next;
    unsigned char *guard;
    unsigned char *r;
    unsigned char *t;

    /* Grow into an adjacent free block where the leftover slack after growing
     * is below the split threshold (< 6 bytes).  The absorbed block must be
     * merged into the returned block, not left marked free: otherwise a later
     * allocation would hand out memory overlapping the grown block. */
    p = (unsigned char *)malloc(48U);
    next = (unsigned char *)malloc(4U);
    guard = (unsigned char *)malloc(16U);
    if (p == 0 || next == 0 || guard == 0)
        fail("grow-absorb setup malloc failed");
    fill(p, 48U, 25);
    free(next);

    /* combined = 48 + (2+2) + 4 = 56; new = 54 leaves slack 2 (< 6), so the
     * resize keeps the whole merged block without splitting a tail. */
    r = (unsigned char *)realloc(p, 54U);
    if (r != p)
        fail("grow-absorb did not stay in place");
    check(r, 48U, 25, "grow-absorb lost contents");

    /* Probe BEFORE writing into the absorbed region: if the next block is
     * still (wrongly) marked free, this first-fit request reuses it and the
     * returned pointer falls inside the grown block. */
    t = (unsigned char *)malloc(4U);
    if (t == 0)
        fail("grow-absorb probe malloc failed");
    if ((unsigned)t >= (unsigned)r && (unsigned)t < (unsigned)r + 54U)
        fail("grow-absorb left overlapping free block");

    fill(r, 54U, 26);
    check(r, 54U, 26, "grow-absorb block not fully usable");

    free(t);
    free(guard);
    free(r);
}

static void t_grow_next_too_small(void)
{
    unsigned char *dest;
    unsigned char *separator;
    unsigned char *p;
    unsigned char *next;
    unsigned char *guard;
    unsigned char *r;
    unsigned int i;

    /* The adjacent free block cannot satisfy the growth, so realloc must use
     * the earlier free destination and copy only p's original 48 bytes. */
    dest = (unsigned char *)malloc(100U);
    separator = (unsigned char *)malloc(8U);
    p = (unsigned char *)malloc(48U);
    next = (unsigned char *)malloc(4U);
    guard = (unsigned char *)malloc(16U);
    if (dest == 0 || separator == 0 || p == 0 || next == 0 || guard == 0)
        fail("grow-too-small setup malloc failed");
    fill(dest, 100U, 27);
    fill(p, 48U, 28);
    free(dest);
    free(next);

    r = (unsigned char *)realloc(p, 80U);
    if (r != dest)
        fail("grow-too-small did not use fallback block");
    check(r, 48U, 28, "grow-too-small lost contents");
    for (i = 48U; i < 80U; i++) {
        if (r[i] != patt(27, i))
            fail("grow-too-small copied beyond old block");
    }

    free(r);
    free(separator);
    free(guard);
}

static void t_trim(void)
{
    unsigned char *p;
    unsigned char *q;
    unsigned char *r;

    /* Freeing the heap-top block must lower the heap limit so the space is
     * fully reclaimed: a later, larger request reuses the same base address
     * instead of growing the heap above the stranded free block. */
    p = (unsigned char *)malloc(4000U);
    if (p == 0)
        fail("trim setup malloc failed");
    free(p);
    q = (unsigned char *)malloc(4000U);
    if (q != p)
        fail("heap did not reuse freed top block");
    free(q);
    r = (unsigned char *)malloc(8000U);
    if (r != p)
        fail("heap did not trim on free of top block");
    free(r);
}

static void t_calloc_overflow(void)
{
    unsigned char *p;

    /* nmemb * size that overflows 16 bits must fail rather than return a
     * wrapped, too-small block. */
    p = (unsigned char *)calloc(4096U, 16U);    /* 65536 -> wraps to 0 */
    if (p != 0)
        fail("calloc overflow (wrap to 0) not rejected");
    p = (unsigned char *)calloc(700U, 100U);    /* 70000 -> wraps to 4464 */
    if (p != 0)
        fail("calloc overflow (partial wrap) not rejected");
    /* a large but non-overflowing product still succeeds and is zeroed */
    p = (unsigned char *)calloc(100U, 100U);
    if (p == 0)
        fail("valid large calloc rejected");
    zcheck(p, 10000U, "calloc did not zero large block");
    free(p);
}

int main(void)
{
    t_split();
    t_nosplit();
    t_forward();
    t_reverse();
    t_bridge();
    t_sizes();
    t_large();
    t_realloc_size_overflow();
    t_zero();
    t_calloc();
    t_realloc();
    t_wrap();
    t_recoalesce();
    t_rezero_coalesce();
    t_shrink_inplace();
    t_grow_next_free();
    t_grow_absorb_nosplit();
    t_grow_next_too_small();
    t_grow_top();
    t_trim();
    t_calloc_overflow();
    t_stress();

    printf("tallocx: all tests passed\n");
    return 0;
}
