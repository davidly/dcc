#include <stdio.h>

struct Pair {
    char a;
    int b;
    unsigned char c;
};

struct Wrap {
    struct Pair p;
    int z;
    char tail;
};

struct Big {
    char data[40];
    int tag;
};

/* Prototypes before definitions: struct args and struct returns. */
static struct Pair proto_make_pair(char a, int b, unsigned char c);
static struct Wrap proto_make_wrap(struct Pair p, int z, char tail);
static struct Big proto_make_big(char base, int tag);
static int proto_sum_pair(struct Pair p);
static int proto_sum_two(struct Pair a, struct Pair b);
static int proto_sum_mix(int left, struct Pair p, int right);
static int proto_sum_wrap(struct Wrap w);
static int proto_sum_big(struct Big b);

/* Pointer/value-copy cases. */
static void copy_pair_ptr(struct Pair *dst, struct Pair *src);
static struct Pair return_pair_ptr(struct Pair *src);
static struct Pair assign_return_pair_ptr(struct Pair *dst, struct Pair *src);
static void copy_wrap_ptr(struct Wrap *dst, struct Wrap *src);
static struct Wrap return_wrap_ptr(struct Wrap *src);
static void fill_big_ptr(struct Big *dst, char base, int tag);
static struct Big return_big_ptr(struct Big *src);

struct Pair gpair;
struct Pair gpair_array[3];
struct Wrap gwrap;
struct Wrap gwrap_array[2];
struct Big gbig;
struct Big gbig_array[2];

static struct Pair proto_make_pair(char a, int b, unsigned char c)
{
    struct Pair p;
    p.a = a;
    p.b = b;
    p.c = c;
    return p;
}

static struct Wrap proto_make_wrap(struct Pair p, int z, char tail)
{
    struct Wrap w;
    w.p = p;
    w.z = z;
    w.tail = tail;
    return w;
}

static struct Big proto_make_big(char base, int tag)
{
    struct Big b;
    int i;
    for (i = 0; i < 40; i++)
        b.data[i] = base + i;
    b.tag = tag;
    return b;
}

static int proto_sum_pair(struct Pair p)
{
    return p.a + p.b + p.c;
}

static int proto_sum_two(struct Pair a, struct Pair b)
{
    return a.a + a.b + a.c + b.a + b.b + b.c;
}

static int proto_sum_mix(int left, struct Pair p, int right)
{
    return left + p.a + p.b + p.c + right;
}

static int proto_sum_wrap(struct Wrap w)
{
    return w.p.a + w.p.b + w.p.c + w.z + w.tail;
}

static int proto_sum_big(struct Big b)
{
    return b.data[0] + b.data[1] + b.data[2] +
           b.data[10] + b.data[20] + b.data[39] + b.tag;
}

static void copy_pair_ptr(struct Pair *dst, struct Pair *src)
{
    *dst = *src;
}

static struct Pair return_pair_ptr(struct Pair *src)
{
    return *src;
}

static struct Pair assign_return_pair_ptr(struct Pair *dst, struct Pair *src)
{
    *dst = *src;
    return *dst;
}

static void copy_wrap_ptr(struct Wrap *dst, struct Wrap *src)
{
    *dst = *src;
}

static struct Wrap return_wrap_ptr(struct Wrap *src)
{
    return *src;
}

static void fill_big_ptr(struct Big *dst, char base, int tag)
{
    *dst = proto_make_big(base, tag);
}

static struct Big return_big_ptr(struct Big *src)
{
    return *src;
}

int main(int argc, char **argv)
{
    struct Pair x;
    struct Pair y;
    struct Pair arr[2];
    struct Pair p;
    struct Pair q;
    struct Wrap w;
    struct Wrap wx;
    struct Big big;
    struct Big bigarr[2];
    struct Big b;
    struct Big c;
    struct Pair init_decl = proto_make_pair(8, 600, 14);
    int s;

    x.a = 3;
    x.b = 1000;
    x.c = 7;
    y = x;
    gpair = y;
    s = proto_sum_pair(gpair);
    printf("assign/arg %d %d %d %d\n", y.a, y.b, y.c, s);

    y = proto_make_pair(4, 2000, 8);
    s = proto_sum_pair(y);
    printf("return %d %d %d %d\n", y.a, y.b, y.c, s);

    printf("init-return %d %d %d %d\n", init_decl.a, init_decl.b, init_decl.c, proto_sum_pair(init_decl));

    arr[0] = proto_make_pair(5, 3000, 9);
    arr[1] = y;
    s = proto_sum_two(arr[0], arr[1]);
    printf("array/multi %d %d %d %d\n", arr[0].a, arr[0].b, arr[0].c, s);

    w = proto_make_wrap(arr[0], 11, 12);
    gwrap = w;
    s = proto_sum_wrap(gwrap);
    printf("nested %d %d %d %d %d %d\n", w.p.a, w.p.b, w.p.c, w.z, w.tail, s);

    s = proto_sum_mix(100, proto_make_pair(6, 4000, 10), 20);
    printf("return-as-arg %d\n", s);

    arr[1] = proto_make_pair(7, 5000, 13);
    s = proto_sum_pair(arr[1]);
    printf("return-to-array %d %d %d %d\n", arr[1].a, arr[1].b, arr[1].c, s);

    big = proto_make_big(1, 6000);
    gbig = big;
    s = proto_sum_big(gbig);
    printf("big %d %d %d %d %d %d %d\n",
           big.data[0], big.data[1], big.data[2], big.data[10], big.data[20], big.data[39], s);

    bigarr[1] = proto_make_big(2, 7000);
    s = proto_sum_big(bigarr[1]);
    printf("big-array-return %d %d %d %d\n", bigarr[1].data[0], bigarr[1].data[39], bigarr[1].tag, s);

    p = proto_make_pair(3, 1000, 7);
    q = proto_make_pair(4, 2000, 8);
    printf("proto pair %d %d %d\n", proto_sum_pair(p), proto_sum_two(p, q), proto_sum_mix(10, p, 20));

    w = proto_make_wrap(p, 11, 12);
    printf("proto wrap %d\n", proto_sum_wrap(w));

    b = proto_make_big(1, 6000);
    printf("proto big %d\n", proto_sum_big(b));

    copy_pair_ptr(&q, &p);
    gpair = return_pair_ptr(&q);
    printf("ptr pair %d %d %d %d\n", gpair.a, gpair.b, gpair.c, proto_sum_pair(gpair));

    gpair_array[0] = proto_make_pair(5, 3000, 9);
    copy_pair_ptr(&gpair_array[1], &gpair_array[0]);
    gpair_array[2] = assign_return_pair_ptr(&q, &gpair_array[1]);
    printf("ptr array %d %d %d %d\n", q.a, q.b, q.c, proto_sum_pair(gpair_array[2]));

    copy_wrap_ptr(&wx, &w);
    gwrap = return_wrap_ptr(&wx);
    printf("ptr wrap %d %d %d %d %d\n", gwrap.p.a, gwrap.p.b, gwrap.p.c, gwrap.z, gwrap.tail);

    fill_big_ptr(&c, 2, 7000);
    gbig = return_big_ptr(&c);
    gbig_array[1] = return_big_ptr(&gbig);
    printf("ptr big %d %d %d\n", gbig.data[0], gbig.data[39], proto_sum_big(gbig_array[1]));

    printf("tstructval5 completed\n");
    return 0;
}
