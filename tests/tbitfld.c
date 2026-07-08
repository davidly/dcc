#include <stdio.h>

struct Bits {
    unsigned a:3;
    unsigned b:5;
    unsigned c:8;
    int d;
};

struct SignedBits {
    int a:4;
    unsigned b:4;
    int c;
};

static struct Bits g = { 5, 17, 200, 1234 };
static struct SignedBits gs = { -3, 12, 77 };

static struct Bits make_bits(unsigned a, unsigned b, unsigned c, int d)
{
    struct Bits x;
    x.a = a;
    x.b = b;
    x.c = c;
    x.d = d;
    return x;
}

static struct SignedBits make_signed(int a, unsigned b, int c)
{
    struct SignedBits x;
    x.a = a;
    x.b = b;
    x.c = c;
    return x;
}

static int sum_bits(struct Bits x)
{
    return x.a + x.b + x.c + x.d;
}

static int sum_signed(struct SignedBits x)
{
    return x.a + x.b + x.c;
}

int main(void)
{
    struct Bits l = { 3, 7, 99, 456 };
    struct Bits m;
    struct SignedBits ls = { -2, 5, 20 };
    struct SignedBits ms;
    int post;
    int pre;

    printf("global %u %u %u %d %d\n", g.a, g.b, g.c, g.d, sum_bits(g));
    printf("local %u %u %u %d %d\n", l.a, l.b, l.c, l.d, sum_bits(l));

    m = make_bits(6, 31, 255, 1000);
    printf("return %u %u %u %d %d\n", m.a, m.b, m.c, m.d, sum_bits(m));

    m.a = 2; m.b = 4; m.c = 8; m.d = 16;
    printf("assign %u %u %u %d %d\n", m.a, m.b, m.c, m.d, sum_bits(m));

    m.a = 1; m.b = 3; m.c = 4; m.d = 0;
    m.a += 3;
    m.c <<= 2;
    post = m.a++;
    pre = ++m.b;
    --m.c;
    m.b -= 2;
    printf("rmw %u %u %u %d %d\n", m.a, m.b, m.c, post, pre);

    /* Live-result read-modify-write that overflows the field width: the
     * returned value must be the stored (truncated / sign-extended) field
     * value, not the raw arithmetic result. */
    m.a = 7; m.b = 7; m.c = 200; m.d = 0;
    pre = ++m.a;                 /* a(3) -> 0, pre -> 0 */
    post = (m.b += 300);         /* b(5) wraps, post == stored b */
    printf("rmw-live %u %d %d %d\n", m.a, pre, m.b, post);
    ms.a = 5; ms.b = 0; ms.c = 0;
    pre = (ms.a += 4);           /* a(4 signed) -> 9 -> -7 */
    post = ms.a--;               /* post -> -7, a -> -8 */
    printf("rmw-live-signed %d %d %d\n", ms.a, pre, post);

    printf("signed-global %d %u %d %d\n", gs.a, gs.b, gs.c, sum_signed(gs));
    printf("signed-local %d %u %d %d\n", ls.a, ls.b, ls.c, sum_signed(ls));
    ms = make_signed(-4, 15, 30);
    printf("signed-return %d %u %d %d\n", ms.a, ms.b, ms.c, sum_signed(ms));
    ms.a = -1; ms.b = 14; ms.c = 40;
    printf("signed-assign %d %u %d %d\n", ms.a, ms.b, ms.c, sum_signed(ms));

    printf("tbitfield completed\n");
    return 0;
}
