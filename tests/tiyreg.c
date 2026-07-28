/* tiyreg.c - whole-function IY register allocation (REG_IY,
 * find_iy_regalloc_candidate / try_speculative_iy_regalloc_function_body).
 *
 * IY is the only register dcc can allocate in a function that CONTAINS CALLS.
 * It is callee-saved (the function pushes the caller's IY ahead of its frame
 * and pops it after restoring IX), DCCRTL contains no IY instruction at all -
 * verified by scripts/rtl-iy-safety.py - and CP/M's 8080-coded BDOS has no
 * index registers to clobber it with. Every caller-saved register (BC, DE, E)
 * is disqualified from such a function outright, which is why these bodies
 * previously received no register allocation of any kind.
 *
 * The helpers called here have EXTERNAL linkage on purpose. A small static
 * callee may be inline-substituted, after which the caller no longer counts
 * as having a call and IY is correctly not attempted - so a version of this
 * test built on static helpers silently stops testing anything.
 *
 * What each case pins down:
 *
 *   walk()      the ordinary win: a read-only pointer parameter referenced
 *               every iteration of a loop that also calls out every
 *               iteration. Verifies the promoted value survives the calls.
 *
 *   fields()    struct field reads through a promoted pointer - the commonest
 *               shape a pointer parameter appears in, and the one that used
 *               to disqualify its own pointer from promotion, because
 *               `p->field` fell into the address-of path and tripped
 *               g_regalloc_address_escaped.
 *
 *   nested()    IY held across a call to ANOTHER IY-promoted function, which
 *               is what actually exercises the callee-save contract. This is
 *               the shape that caught the wumpus.c miscompile, where dccpeep
 *               independently borrowed IY inside a callee and destroyed its
 *               caller's promoted pointer.
 *
 *   written()   negative control: the parameter is assigned, so it must not
 *               be promoted - a wrong answer here shows up as a stale read.
 *
 *   addrof()    negative control: the parameter's own address escapes, so a
 *               register copy could desync from writes made through the alias.
 *
 * Self-checking with a deterministic transcript, so it doubles as a clang
 * baseline: any divergence is a real miscompile, not a formatting difference.
 */
#include <stdio.h>
#include <stdlib.h>

struct Node {
    int id;
    int weight;
    int flags;
};

int consume(int v);
void bump(const int **pp);

int consume(int v)
{
    return abs(v) * 2 + 1;
}

/* Read-only pointer parameter, referenced every iteration, calling out every
 * iteration. The calls are to library routines rather than to anything
 * defined here, so they are unambiguously calls: a same-file callee can be
 * inline-substituted, and once it is, the caller no longer contains a call
 * and IY is correctly not attempted. */
int walk(const int *a, int n)
{
    int i;
    int total = 0;

    for (i = 0; i < n; ++i)
        total += abs(a[i]) + a[i] + abs(a[i] + 1) + abs(a[i] - 2) + abs(a[i] * 3);
    return total;
}

/* Field reads through a promoted pointer, interleaved with calls. */
int fields(const struct Node *p, int reps)
{
    int i;
    int total = 0;

    for (i = 0; i < reps; ++i) {
        total += abs(p->id);
        total += abs(p->weight);
        total += abs(p->flags);
        total += p->id + p->weight + p->flags;
    }
    return total;
}

/* Holds its own promoted pointer across a call to another promoted function. */
int nested(const int *a, int n)
{
    int i;
    int total = 0;

    for (i = 0; i < n; ++i) {
        total += walk(a, n);
        total += a[i];
        total += abs(a[i]) + abs(a[i] + 1) + abs(a[i] - 1);
    }
    return total;
}

/* Negative control: the parameter itself is written, so promotion must be
 * declined - the frame slot is the only correct home for it. */
int written(const int *a, int n)
{
    int total = 0;

    while (n-- > 0) {
        total += abs(*a);
        total += *a + abs(*a + 1) + abs(*a - 1);
        ++a;
        total += abs(*a - 1) + abs(*a + 2);
    }
    return total;
}

void bump(const int **pp)
{
    *pp = *pp + 1;
}

/* Negative control: the parameter's own address escapes into bump(). */
int addrof(const int *a, int n)
{
    int total = 0;

    while (n-- > 0) {
        total += abs(*a);
        total += *a + abs(*a + 1) + abs(*a - 1);
        bump(&a);
        total += abs(n) + abs(n + 1);
    }
    return total;
}

int main(void)
{
    static const int data[7] = { 3, 1, 4, 1, 5, 9, 2 };
    struct Node node;

    node.id = 7;
    node.weight = 11;
    node.flags = 13;

    printf("walk    = %d\n", walk(data, 6));
    printf("fields  = %d\n", fields(&node, 4));
    printf("nested  = %d\n", nested(data, 4));
    printf("written = %d\n", written(data, 6));
    printf("addrof  = %d\n", addrof(data, 6));
    return 0;
}
