/* BUG-069: sizeof a two-dimensional array struct member, or a partially
 * indexed one, reports the size of the innermost element row.
 *
 * `ast_sizeof_expr_value()` sizes an AST_MEMBER array field as
 * `element_size * array_length`, where the element is the innermost scalar
 * and the length only the first dimension, so `int16_t c[2][2]` is 4 rather
 * than 8. `ast_expr_type_for_sizeof()` types a partially indexed member
 * `t.c[1]` as the scalar element, so it reports 2 rather than 4. Function
 * scope and nested blocks fail alike; a direct `int16_t c[2][2]` local and a
 * fully indexed `t.c[1][0]` are correct.
 *
 * Expected (clang): 4 8 4 / 4 8 4 / 8 2 12
 * dcc main 3ca3ff90: 2 4 4 / 2 4 4 / 8 2 3
 */
#include <stdio.h>
#include <stdint.h>

struct T { int32_t a[3]; uint8_t b; int16_t c[2][2]; };
struct U { uint8_t m[3][4]; };
int g = 3;

int main(int argc, char **argv)
{
    struct T t;
    struct T *pt = &t;
    struct T arr[2];
    struct U u;
    int16_t direct[2][2];
    (void)argv;
    t.b = 0; pt->b = 0; arr[0].b = 0; u.m[0][0] = 0; direct[0][0] = 0;
    printf("%d %d %d\n", (int)sizeof pt->c[1], (int)sizeof arr[1].c, (int)sizeof(*(int32_t *)&argc + g));
    printf("%d %d %d\n", (int)sizeof t.c[1], (int)sizeof t.c, (int)sizeof arr[1].a[0]);
    printf("%d %d %d\n", (int)sizeof direct, (int)sizeof t.c[1][0], (int)sizeof u.m);
    return 0;
}
