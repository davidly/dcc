/* Float prefix/postfix increment and decrement on locals, globals, members,
 * dereferences, pointer indexes and nested-block arrays; compare += and +. */
#include <stdint.h>
#include <stdio.h>

static float fg = 1.0f;
static float fa[4] = { 1.0f, 2.0f, 3.0f, -3.491f };
static float *p = fa;
static struct S { float m; } s = { 1.0f };

static void show(const char *tag, float f)
{
    union { float f; uint32_t u; } u;
    u.f = f;
    printf("%s %08lx\n", tag, (unsigned long)u.u);
}

int main(void)
{
    float l = 1.0f;
    int i = 2;

    l++;                show("local ++", l);           /* 40000000 */
    --l;                show("local --", l);           /* 3f800000 */
    fg++;               show("global ++", fg);         /* 40000000 */
    s.m++;              show("member ++", s.m);        /* 40000000 */
    (*p)++;             show("deref ++", *p);          /* 40000000 */
    fa[1]++;            show("element ++", fa[1]);     /* 40400000 */
    fa[i]--;            show("element --", fa[i]);     /* 40000000 */
    p[3]++;             show("pointer index ++", p[3]); /* c01f6c8b */
    {
        float la[2] = { 2.5f, 0.0f };
        la[0]++;        show("local element ++", la[0]); /* 40600000 */
        la[1]++;        show("local element ++", la[1]); /* 3f800000 */
    }
    l = 1.0f;
    l += 1.0f;          show("+= (correct)", l);       /* 40000000 */
    l = l + 1;          show("+ 1 (correct)", l);       /* 40400000 */
    return 0;
}
