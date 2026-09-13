#include <stdio.h>

#ifndef MIR_CLOBBER_MAKE_B
#define MIR_CLOBBER_MAKE_B 5000
#endif

struct Pair {
    unsigned char a;
    unsigned int b;
    unsigned char c;
};

union UPair {
    struct Pair p;
    unsigned long l;
    char bytes[8];
};

union UName {
    char name[4];
    unsigned long l;
};

static union UPair g_up = { { 3, 1000, 7 } };
static union UName g_name = { "abc" };
static union UPair g_arr[2] = {
    { { 4, 2000, 8 } },
    { { 5, 3000, 9 } }
};

static unsigned long pair_sum(struct Pair p)
{
    return (unsigned long)p.a + p.b + p.c;
}

static unsigned long upair_sum(union UPair u)
{
    return pair_sum(u.p);
}

static union UPair make_upair(unsigned char a, unsigned int b, unsigned char c)
{
    union UPair u;
    u.p.a = a;
    u.p.b = b;
    u.p.c = c;
    return u;
}

static void copy_through_pointer(union UPair *dst, union UPair *src)
{
    *dst = *src;
}

int main(int argc, char **argv)
{
    union UPair l_up = { { 6, 4000, 10 } };
#ifdef MIR_CLOBBER_LOCAL_NAME_W
    union UName l_name = { "xyzw" };
#else
    union UName l_name = { "xyz" };
#endif
    union UPair a;
    union UPair b;

    printf("global union %d %u %d %lu\n", g_up.p.a, g_up.p.b, g_up.p.c, upair_sum(g_up));
    printf("global name %d %d %d %lu\n", g_name.name[0], g_name.name[1], g_name.name[2], g_name.l);
    printf("global array %lu %lu\n", upair_sum(g_arr[0]), upair_sum(g_arr[1]));
    printf("local union %d %u %d %lu\n", l_up.p.a, l_up.p.b, l_up.p.c, upair_sum(l_up));
    printf("local name %d %d %d %lu\n", l_name.name[0], l_name.name[1], l_name.name[2], l_name.l);

    a = make_upair(7, MIR_CLOBBER_MAKE_B, 11);
#ifdef MIR_CLOBBER_ASSIGN_LOCAL
    b = l_up;
#else
    b = a;
#endif
    printf("return/assign %d %u %d %lu\n", b.p.a, b.p.b, b.p.c, upair_sum(b));

#ifdef MIR_CLOBBER_COPY_TO_A
    copy_through_pointer(&a, &g_arr[1]);
#else
    copy_through_pointer(&b, &g_arr[1]);
#endif
#ifdef MIR_CLOBBER_FINAL_SUM_A
    printf("ptr copy %d %u %d %lu\n", b.p.a, b.p.b, b.p.c, upair_sum(a));
#elif defined(MIR_CLOBBER_FINAL_B_FROM_A)
    printf("ptr copy %d %u %d %lu\n", b.p.a, a.p.b, b.p.c, upair_sum(b));
#else
    printf("ptr copy %d %u %d %lu\n", b.p.a, b.p.b, b.p.c, upair_sum(b));
#endif

    printf("tunion completed\n");
    return 0;
}
