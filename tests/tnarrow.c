#include <stdio.h>
#include <stdint.h>

#define NARR 50

/* Every element is provably in [0,255] via a variable (non-literal) index -
 * the case that must narrow the array's storage to unsigned char and use a
 * matching (not doubled) address stride. Large enough that a stale,
 * word-sized address stride paired with byte-sized storage (the historical
 * bug this guards against) would write past the narrowed frame slot and
 * corrupt an adjacent local or the sum itself. Deliberately mirrors e.c's
 * own `for (n = N - 1; n > 0; --n) a[n] = ...;` shape - the array-narrowing
 * analysis in dcc_array_narrow.c only recognizes this decrementing/`>` -
 * guarded idiom for variable-indexed writes, not a plain `for (i = 0;
 * i < N; i++)` counter (see the memory/commit notes for this feature).
 * `a` must also be the LAST local declared in this scope: the speculative
 * re-parse used to prove safety starts right after its declarator and
 * can't handle a further declaration (only statements), so anything
 * declared after it forces an automatic (safe) decline. */
int narwsum(void)
{
    int N;
    int n;
    int i;
    int total;
    int a[NARR];

    N = NARR;
    for (n = N - 1; n > 0; --n)
        a[n] = n;
    a[0] = 0;

    /* A separate read-only loop over a distinct variable: reusing `n` here
     * (e.g. `n = n + 1`) would fold an unrelated write into `n`'s own
     * dependency-group bound and defeat the proof above - reads don't need
     * a recognized write shape, but this still keeps the two loops'
     * variables independent. */
    total = 0;
    for (i = 0; i < NARR; i = i + 1)
        total = total + a[i];

    return total;
}

/* Stores a negative value: must NOT narrow (unsigned char would wrap it to
 * a large positive value on store, silently corrupting it). */
int narwneg(void)
{
    int i;
    int b[10];
    int total;

    for (i = 0; i < 10; i++)
        b[i] = i - 5;

    total = 0;
    for (i = 0; i < 10; i++)
        total = total + b[i];

    return total;
}

/* Stores values above 255: must NOT narrow. total is int16_t (not plain
 * int) so its expected overflow-wrapped sum is the same on dcc's 16-bit
 * int and a host compiler's wider int. */
int narwbig(void)
{
    int i;
    int c[10];
    int16_t total;

    for (i = 0; i < 10; i++)
        c[i] = i * 1000;

    total = 0;
    for (i = 0; i < 10; i++)
        total = total + c[i];

    return total;
}

int *narwptr;

/* The array's address escapes via a bare (non-indexed) reference: must NOT
 * narrow, since code outside this analysis could store an unbounded value
 * through the escaped pointer. */
int narwesc(void)
{
    int i;
    int d[10];

    for (i = 0; i < 10; i++)
        d[i] = i;

    narwptr = d;

    return narwptr[3];
}

/* Mutually recursive captured return expressions must make narrowing decline
 * rather than recursively walking until the compiler exhausts its host stack.
 * The call stays in a dead runtime branch: this test is about compilation, not
 * executing intentionally recursive functions. */
static int mrright(void);

static int mrleft(void)
{
    return mrright();
}

static int mrright(void)
{
    return mrleft();
}

static int narwrec(void)
{
    int mutual[2];

    if (0)
        mutual[0] = mrleft();
    mutual[0] = 9;
    return mutual[0];
}

/* A finite no-argument call chain remains eligible for proof. */
static int fcbase(void)
{
    return 23;
}

static int fctop(void)
{
    return fcbase();
}

static int narwchain(void)
{
    int values[2];

    values[0] = fctop();
    return values[0];
}

int main()
{
    printf("sum=%d\n", narwsum());
    printf("neg=%d\n", narwneg());
    printf("large=%d\n", narwbig());
    printf("esc=%d\n", narwesc());
    printf("rec=%d\n", narwrec());
    printf("chain=%d\n", narwchain());
    return 0;
}
