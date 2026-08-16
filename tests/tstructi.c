#include <stdio.h>

#ifndef MIR_CLOBBER_G_PAIR_A
#define MIR_CLOBBER_G_PAIR_A 3
#endif

typedef unsigned char u8;
typedef unsigned int u16;

struct Pair {
    u8 a;
    int b;
    u8 c;
};

struct Wrap {
    struct Pair p;
    u8 x;
    u16 y0;
    u16 y1;
};

struct BigInit {
    u8 a0;
    u8 a1;
    u8 a2;
    u8 a3;
    u16 w;
    struct Pair p;
};

static struct Pair g_pair = { MIR_CLOBBER_G_PAIR_A, 1000, 7 };
static struct Wrap g_wrap = { { 4, 2000, 8 }, 9, 10, 11 };
static struct Pair g_pairs[2] = { { 5, 3000, 9 }, { 6, 4000, 10 } };

static int sum_pair( struct Pair p )
{
    return p.a + p.b + p.c;
}

int main( int argc, char *argv[] )
{
    struct Pair l_pair = { 7, 5000, 13 };
    struct Wrap l_wrap = { { 8, 6000, 14 }, 15, 16, 17 };
    struct Pair l_pairs[2] = { { 9, 7000, 18 }, { 10, 8000, 19 } };
    struct BigInit big = { 1, 2, 3, 4, 9000, { 20, 10000, 21 } };
    struct Pair partial = { 22 };

    printf( "global pair %d %d %d %d\n", g_pair.a, g_pair.b, g_pair.c, sum_pair( g_pair ) );
    printf( "global wrap %d %d %d %d %d %d\n", g_wrap.p.a, g_wrap.p.b, g_wrap.p.c, g_wrap.x, g_wrap.y0, g_wrap.y1 );
    printf( "global array %d %d %d %d\n", g_pairs[0].a, g_pairs[1].b, g_pairs[1].c, sum_pair( g_pairs[1] ) );

    printf( "local pair %d %d %d %d\n", l_pair.a, l_pair.b, l_pair.c, sum_pair( l_pair ) );
    printf( "local wrap %d %d %d %d %d %d\n", l_wrap.p.a, l_wrap.p.b, l_wrap.p.c, l_wrap.x, l_wrap.y0, l_wrap.y1 );
    printf( "local array %d %d %d %d\n", l_pairs[0].a, l_pairs[1].b, l_pairs[1].c, sum_pair( l_pairs[1] ) );
    printf( "big %d %d %d %d %d %d %d\n", big.a0, big.a1, big.a2, big.a3, big.w, big.p.b, big.p.c );
    printf( "partial %d %d %d\n", partial.a, partial.b, partial.c );
    printf( "tstructinit completed\n" );
    return 0;
}
