/* Regression test for a real dcc bug (fixed in ast_expr_type_for_sizeof's
 * AST_INDEX case, dcc_ast_build.c): a struct member declared as a pointer
 * ARRAY (`struct Foo *arr[N];`) read back mis-typed as a plain int array at
 * the first use - `p = f.arr[0];` (p a pointer) failed with "incompatible
 * integer to pointer assignment". Not specific to self-reference: the same
 * struct's own type is used here for the array's pointee (a common "graph
 * of nodes" shape) purely because that's the natural test to write, but a
 * minimal repro during the original investigation reproduced identically
 * for a pointer array to an unrelated, already-complete struct type. Plain
 * (non-array) pointer members were never affected.
 *
 * Root cause: for `n = f.arr[0]` (an AST_INDEX over an AST_MEMBER), the
 * AST_MEMBER case already returns the field's ELEMENT type (fd->elem_type)
 * for an array field - i.e. arr[0]'s own type, already "post-indexing".
 * The AST_INDEX case's generic fallback didn't know that, and unconditionally
 * decayed the result one more level (assuming it still needed to dereference
 * a pointer being indexed) - stripping a one-deep pointer element type down
 * to zero. A plain int array field never triggered this (nothing to decay),
 * only pointer-array fields did. There was already a matching special case
 * for a bare array *variable* (AST_IDENT) avoiding this same double-decay;
 * it was just missing for a struct/union array *field* (AST_MEMBER). */
#include <stdio.h>

struct Node {
    int val;
    struct Node *kids[3];
};

static struct Node nodes[3];

int main(void)
{
    struct Node *p;
    struct Node *root;
    int i;

    for (i = 0; i < 3; i++)
        nodes[i].val = i * 10;

    nodes[0].kids[0] = &nodes[1];
    nodes[0].kids[1] = &nodes[2];
    nodes[0].kids[2] = &nodes[0];

    p = nodes[0].kids[0];
    printf("via dot: %d\n", p->val);

    root = &nodes[0];
    p = root->kids[1];
    printf("via arrow: %d\n", p->val);

    p = nodes[0].kids[2]->kids[0];
    printf("via chain: %d\n", p->val);

    return 0;
}
