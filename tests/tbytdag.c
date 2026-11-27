/* Demanded low-byte arithmetic, operand order and signed return extension. */
#include <stdio.h>
#define CASE(N,K) \
static unsigned char a##N(unsigned x){return (unsigned char)(x*(K)+(x>>8));} \
static unsigned char b##N(unsigned x){return (unsigned char)(((K)*x)^(x>>8));} \
static signed char c##N(unsigned x){return (signed char)(x*(K)-(x>>8));}
CASE(0,0U)
CASE(1,1U)
CASE(2,2U)
CASE(3,3U)
CASE(4,5U)
CASE(5,37U)
CASE(6,128U)
CASE(7,255U)
CASE(8,257U)
CASE(9,65535U)
struct Test {unsigned k;unsigned char (*a)(unsigned);unsigned char (*b)(unsigned);signed char (*c)(unsigned);};
static const struct Test tests[]={
{0U,a0,b0,c0},
{1U,a1,b1,c1},
{2U,a2,b2,c2},
{3U,a3,b3,c3},
{5U,a4,b4,c4},
{37U,a5,b5,c5},
{128U,a6,b6,c6},
{255U,a7,b7,c7},
{257U,a8,b8,c8},
{65535U,a9,b9,c9}
};
static unsigned product(unsigned x,unsigned k) {
    unsigned result=0;
    while(k){if(k&1U)result+=x;x+=x;k>>=1;}
    return result;
}
static unsigned calls;
static unsigned once(unsigned x){++calls;return x;}
static unsigned char effect(unsigned x){return (unsigned char)(once(x)*37U+(x>>8));}
static unsigned char narrow(unsigned char x){return (unsigned char)(x+(x>>8));}
static unsigned char nested(unsigned x,unsigned y){return (unsigned char)((x*37U+(x>>8))-(y*5U+(y>>8)));}
int main(void) {
    static const unsigned values[]={0U,1U,127U,128U,255U,256U,257U,32767U,32768U,65535U};
    unsigned i,j;
    for(i=0;i<sizeof(tests)/sizeof(tests[0]);++i)for(j=0;j<512U;++j) {
        unsigned x=j<10U?values[j]:(j*127U+19U)&65535U;
        unsigned p=product(x,tests[i].k)&65535U;
        unsigned high=x>>8;
        unsigned low=(p-high)&255U;
        int signed_low=low<128U?(int)low:(int)low-256;
        if(tests[i].a(x)!=((p+high)&255U) || tests[i].b(x)!=((p^high)&255U) ||
           tests[i].c(x)!=signed_low)return puts("FAIL byte arithmetic");
        if(narrow((unsigned char)x)!=(x&255U))return puts("FAIL promoted narrow input");
    }
    for(i=0;i<256U;++i) {
        unsigned x=(i*257U)&65535U,y=(65535U-x)&65535U;
        unsigned want=(product(x,37U)+(x>>8)-product(y,5U)-(y>>8))&255U;
        if(nested(x,y)!=want)return puts("FAIL nested scratch or argument address");
        calls=0;
        if(effect(x)!=((product(x,37U)+(x>>8))&255U) || calls!=1)return puts("FAIL byte effects");
    }
    puts("tbytdag passed");return 0;
}
