/* Regression test: `x[K] = x[K] + delta;` where x is a cast-from-void*
 * pointer and K is a compile-time-constant index, must add delta plain -
 * not scale it by the element size as if it were itself an array index.
 *
 * ast_pointer_expr_type's AST_INDEX case used to accept
 * ast_index_pointer_expr_elem_type's result without checking that the
 * indexed element type was actually a pointer. That helper's own contract
 * is just "what type does indexing this pointer expression produce" -
 * pointer or not - which is right for its other caller (the address
 * codegen that wants the indexed VALUE's type regardless), but wrong for
 * a "is this whole expression itself pointer-typed" check. For `((int
 * *)p)[0]` (element type plain int, not a pointer), the check should have
 * failed but didn't, so the '+' on the right-hand side of the assignment
 * took the pointer-arithmetic fast path and scaled `delta` by sizeof(int)
 * - silently doubling it for a 2-byte element.
 *
 * Traced back from tests/cobint.c's documented, previously-abandoned
 * bump_var/check_idx_bump optimization (a combined var_get+delta+var_set
 * through exactly this cast-and-index shape) - reproduced independent of
 * inlining, struct nesting, or which index value is used. */

#include <stdio.h>

static int failures;

static void check(const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s got=%d expected=%d\n", name, got, expected);
        failures++;
    }
}

/* Minimal shape: cast a void* to int*, index at a compile-time constant 0,
 * on both sides of a plain-int '+'. This is the exact pattern that
 * miscompiled. */
static void bump_word0(void *p, int delta)
{
    ((int *)p)[0] = ((int *)p)[0] + delta;
}

/* Same shape at a non-zero compile-time index, and via a runtime (not
 * compile-time-constant) index - the original cobint.c call sites indexed
 * by a variable, not a literal. */
static void bump_word_at(void *p, int idx, int delta)
{
    ((int *)p)[idx] = ((int *)p)[idx] + delta;
}

/* A read-modify-write through a genuinely pointer-typed element must still
 * scale the delta by the pointer's own size - the fix must not reject
 * real pointer-array arithmetic along with the false positive above. */
static void bump_ptr0(int **pp, int delta)
{
    pp[0] = pp[0] + delta;
}

int main(void)
{
    int word_storage[4];
    char byte_storage[4];
    int target_a;
    int target_b;
    int *ptr_storage[2];

    word_storage[0] = 0;
    bump_word0(&word_storage[0], 5);
    bump_word0(&word_storage[0], 7);
    check("word-index0-accumulate", word_storage[0], 12);

    word_storage[2] = 100;
    bump_word_at(&word_storage[0], 2, 3);
    bump_word_at(&word_storage[0], 2, -1);
    check("word-runtime-index-accumulate", word_storage[2], 102);

    /* Byte-sized elements scale by 1 either way, so this alone would not
     * have caught the bug - kept as a sanity check that the fix does not
     * disturb the already-correct byte path. */
    byte_storage[1] = 10;
    ((char *)&byte_storage[0])[1] = ((char *)&byte_storage[0])[1] + 4;
    check("byte-index-accumulate", byte_storage[1], 14);

    target_a = 111;
    target_b = 222;
    ptr_storage[0] = &target_a;
    bump_ptr0(ptr_storage, 1);
    check("genuine-pointer-array-scaled", (int)(ptr_storage[0] - &target_a), 1);
    (void)target_b;

    printf("tidxrmw %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures != 0;
}
