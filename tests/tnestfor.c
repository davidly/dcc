#include <stdio.h>

#define LIMIT 80

struct Sieve {
    unsigned char composite[LIMIT + 1];
};

/* Nested control flow: `for` whose body is an `if` whose body is another
 * `for`, written in the compact single-line form.  The inner loop's
 * increment `i = i + p` is a variable-stride local self-add. */
static void build_sieve(struct Sieve *s)
{
    int i;
    int p;

    for (i = 0; i <= LIMIT; i = i + 1)
        s->composite[i] = 0;
    s->composite[0] = 1;
    s->composite[1] = 1;
    for (p = 2; p * p <= LIMIT; p = p + 1) if (!s->composite[p]) for (i = p * p; i <= LIMIT; i = i + p) s->composite[i] = 1;
}

static int count_primes(const struct Sieve *s)
{
    int i;
    int count;

    count = 0;
    for (i = 2; i <= LIMIT; i = i + 1)
        if (!s->composite[i])
            count = count + 1;
    return count;
}

static int largest_gap(const struct Sieve *s)
{
    int i;
    int last;
    int best;

    last = -1;
    best = 0;
    for (i = 2; i <= LIMIT; i = i + 1)
        if (!s->composite[i]) {
            if (last >= 0 && i - last > best)
                best = i - last;
            last = i;
        }
    return best;
}

/* Logical-not of indexed wide / pointer elements in a controlling
 * expression.  The long and float cases exercise the pre-existing indexed
 * read gates; the pointer-element and partial-array-subscript cases exercise
 * the shapes newly admitted for conditions. */
struct Bag {
    long lv[4];
    float fv[4];
    char *pv[4];
};

static int nz_long(const struct Bag *b, int i)  { if (!b->lv[i]) return 0; return 1; }
static int nz_float(const struct Bag *b, int i) { if (!b->fv[i]) return 0; return 1; }
static int nz_ptr(const struct Bag *b, int i)   { if (!b->pv[i]) return 0; return 1; }

static char *g_pv[4];
static int g_grid[3][4];

/* `!g_pv[i]` - logical-not of a global pointer-array element. */
static int gp_null(int i) { if (!g_pv[i]) return 1; return 0; }

/* `!g_grid[i]` - a single subscript of a 2D array decays the row to a
 * pointer, so this is always false (the row address is never null). */
static int row_null(int i) { if (!g_grid[i]) return 1; return 0; }

/* Self-add for-increment, `+` and `-` variants, both with a variable stride
 * held in a local (i.e. `a = a + step` / `a = a - step`). */
static int sum_stride(int lo, int hi, int step)
{
    int a;
    int sum;

    sum = 0;
    for (a = lo; a < hi; a = a + step)
        sum = sum + a;
    return sum;
}

static int count_down(int hi, int step)
{
    int a;
    int cnt;

    cnt = 0;
    for (a = hi; a > 0; a = a - step)
        cnt = cnt + 1;
    return cnt;
}

int main(void)
{
    struct Sieve s;
    struct Bag b;
    int i;
    int lmask;
    int fmask;
    int pmask;
    int gmask;
    int rmask;

    build_sieve(&s);

    for (i = 0; i < 4; i = i + 1) {
        b.lv[i] = (long)(i == 2);
        b.fv[i] = (i == 1) ? 1.0f : 0.0f;
        b.pv[i] = (i == 3) ? "x" : (char *)0;
    }
    g_pv[2] = "y";
    for (i = 0; i < 3; i = i + 1) {
        int r;
        for (r = 0; r < 4; r = r + 1)
            g_grid[i][r] = 1;
    }
    g_grid[1][0] = 0;

    lmask = 0;
    fmask = 0;
    pmask = 0;
    gmask = 0;
    rmask = 0;
    for (i = 0; i < 4; i = i + 1) {
        lmask = lmask * 10 + nz_long(&b, i);
        fmask = fmask * 10 + nz_float(&b, i);
        pmask = pmask * 10 + nz_ptr(&b, i);
        gmask = gmask * 10 + gp_null(i);
    }
    for (i = 0; i < 3; i = i + 1)
        rmask = rmask * 10 + row_null(i);

    printf("tnestfor primes=%d gap=%d p79=%d\n",
           count_primes(&s), largest_gap(&s), !s.composite[79]);
    printf("notidx l=%04d f=%04d p=%04d gpnull=%04d rownull=%03d\n",
           lmask, fmask, pmask, gmask, rmask);
    printf("stride sum=%d down=%d\n", sum_stride(0, 10, 2), count_down(20, 3));
    return 0;
}