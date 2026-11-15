/* sizeof over multi-dimensional array struct members.
 *
 * A whole array member is sized as the field's byte size and a partially
 * indexed member as a row: element size times the remaining dimensions. These
 * once reported the innermost row and the scalar element (BUG-069). Direct
 * arrays, fully indexed members and one-dimensional members are the control.
 * Baseline proven with clang.
 */
#include <stdio.h>
#include <stdint.h>

struct T { int32_t a[3]; uint8_t b; uint8_t pad[3]; int16_t c[2][2]; };
struct U { uint8_t m[3][4]; int32_t w[2][3][2]; };
struct V { struct T inner[2]; };
static struct U *pu;

static struct T gt;
static struct U gu;
static struct V gv;
static struct V *pgv = &gv;

static void function_scope(void)
{
    struct T t;
    struct T *pt = &t;
    struct T arr[2];
    struct U u;
    int16_t direct[2][2];
    t.b = 0; pt->b = 0; arr[0].b = 0; u.m[0][0] = 0; direct[0][0] = 0;
    printf("fs %d %d %d\n", (int)sizeof pt->c[1], (int)sizeof arr[1].c, (int)sizeof arr[1].a);
    printf("fs %d %d %d\n", (int)sizeof t.c[1], (int)sizeof t.c, (int)sizeof arr[1].a[0]);
    printf("fs %d %d %d\n", (int)sizeof direct, (int)sizeof t.c[1][0], (int)sizeof u.m);
    printf("fs %d %d %d %d\n", (int)sizeof u.w, (int)sizeof u.w[1], (int)sizeof u.w[1][2], (int)sizeof u.w[0][0][1]);
}

static void nested_block(void)
{
    {
        struct U u;
        struct T *pt = &gt;
        u.m[0][0] = 1;
        printf("blk %d %d %d %d\n", (int)sizeof u.m, (int)sizeof u.m[2], (int)sizeof pt->c, (int)sizeof pt->c[0]);
    }
}

static void idioms(void)
{
    struct U u;
    u.m[0][0] = 2;
    printf("idiom %d %d %d\n",
           (int)(sizeof u.m / sizeof u.m[0]),
           (int)(sizeof u.m[0] / sizeof u.m[0][0]),
           (int)(sizeof gu.w / sizeof gu.w[0]));
    printf("glob %d %d %d %d\n", (int)sizeof gt.c, (int)sizeof gu.m[1], (int)sizeof pgv->inner, (int)sizeof pgv->inner[1].c[0]);
    pu = &gu;
    printf("ptr %d %d %d\n", (int)sizeof pu->m, (int)sizeof pu->w[0], (int)sizeof pu->w[0][1]);
    printf("struct %d %d %d\n", (int)sizeof(struct T), (int)sizeof(struct U), (int)sizeof(struct V));
}

int main(void)
{
    function_scope();
    nested_block();
    idioms();
    return 0;
}
