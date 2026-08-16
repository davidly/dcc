#include <stdio.h>
#include <stdint.h>

#ifndef MIR_CLOBBER_G_NAME_V
#define MIR_CLOBBER_G_NAME_V 1000
#endif

struct Name {
    char s[4];
    unsigned int v;
};

struct WrapName {
    unsigned char tag;
    struct Name n;
    char tail[3];
};

static struct Name g_name = { "abc", MIR_CLOBBER_G_NAME_V };
static struct Name g_short = { "x", 2000 };
static struct WrapName g_wrap = { 7, { "yz", 3000 }, "pq" };
static struct Name g_arr[2] = { { "hi", 4000 }, { "bye", 5000 } };

static unsigned sum4(char *p)
{
    return (unsigned)(unsigned char)p[0] + (unsigned)(unsigned char)p[1] +
           (unsigned)(unsigned char)p[2] + (unsigned)(unsigned char)p[3];
}

static unsigned sum3(char *p)
{
    return (unsigned)(unsigned char)p[0] + (unsigned)(unsigned char)p[1] +
           (unsigned)(unsigned char)p[2];
}

int main(void)
{
    struct Name l_name = { "dog", 6000 };
    struct Name l_short = { "q", 7000 };
    struct WrapName l_wrap = { 8, { "rs", 8000 }, "tu" };
    struct Name l_arr[2] = { { "no", 9000 }, { "cat", 10000 } };

    printf("global name %u %u %u\n", sum4(g_name.s), g_name.v, sum4(g_short.s));
    printf("global wrap %u %u %u %u\n", g_wrap.tag, sum4(g_wrap.n.s), g_wrap.n.v, sum3(g_wrap.tail));
    printf("global array %u %u %u %u\n", sum4(g_arr[0].s), g_arr[0].v, sum4(g_arr[1].s), g_arr[1].v);

    printf("local name %u %u %u\n", sum4(l_name.s), l_name.v, sum4(l_short.s));
    printf("local wrap %u %u %u %u\n", l_wrap.tag, sum4(l_wrap.n.s), l_wrap.n.v, sum3(l_wrap.tail));
    printf("local array %u %u %u %u\n", sum4(l_arr[0].s), l_arr[0].v, sum4(l_arr[1].s), l_arr[1].v);

    printf("tstructinitstr completed\n");
    return 0;
}
