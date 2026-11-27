/* Fused logical shifts, bounded score decisions, call snapshots and exits. */
#include <stdint.h>
#include <stdio.h>
static volatile unsigned runtime_count;
static volatile uint32_t observed;
static unsigned calls;
#define SHIFT(N) static uint32_t left##N(uint32_t x) { return x^(x<<N); } \
 static uint32_t right##N(uint32_t x) { return (x>>N)^x; }
SHIFT(0)
SHIFT(1)
SHIFT(2)
SHIFT(3)
SHIFT(4)
SHIFT(5)
SHIFT(6)
SHIFT(7)
SHIFT(8)
SHIFT(9)
SHIFT(10)
SHIFT(11)
SHIFT(12)
SHIFT(13)
SHIFT(14)
SHIFT(15)
SHIFT(16)
SHIFT(17)
SHIFT(18)
SHIFT(19)
SHIFT(20)
SHIFT(21)
SHIFT(22)
SHIFT(23)
SHIFT(24)
SHIFT(25)
SHIFT(26)
SHIFT(27)
SHIFT(28)
SHIFT(29)
SHIFT(30)
SHIFT(31)
struct Shift { uint32_t (*left)(uint32_t); uint32_t (*right)(uint32_t); };
static const struct Shift shifts[]={
{left0,right0},
{left1,right1},
{left2,right2},
{left3,right3},
{left4,right4},
{left5,right5},
{left6,right6},
{left7,right7},
{left8,right8},
{left9,right9},
{left10,right10},
{left11,right11},
{left12,right12},
{left13,right13},
{left14,right14},
{left15,right15},
{left16,right16},
{left17,right17},
{left18,right18},
{left19,right19},
{left20,right20},
{left21,right21},
{left22,right22},
{left23,right23},
{left24,right24},
{left25,right25},
{left26,right26},
{left27,right27},
{left28,right28},
{left29,right29},
{left30,right30},
{left31,right31}
};
static unsigned word_score(unsigned x)
{
    unsigned n=0;
    if(x==257U)n+=1;
    if(x!=257U)n+=2;
    if(!(x==257U))n+=4;
    if(!(x!=257U))n+=8;
    return n+((x>>8)*16U);
}
static int evolving(uint32_t a,uint32_t b)
{
    int i,n=0;
    for(i=0;i<4;++i) {
        if(a==b)n+=1;
        a^=1UL;
        if(a==b)n+=2;
    }
    return n;
}
static unsigned once(unsigned x) { ++calls; return x; }
static unsigned char byte_result(unsigned x) { ++calls; return (unsigned char)x; }
static unsigned wrapped(unsigned x)
{
    unsigned value=once(x),score=0;
    if(value==10U)score=3;
    return value+score;
}
static int exact_break(unsigned char *p,unsigned char first)
{
    int i;
    for(i=3;i<13;++i) {
        p[i]=first+i;
        if(i==7)break;
    }
    return i;
}
static int miss_break(unsigned char *p,unsigned char first)
{
    int i;
    for(i=3;i<13;++i) {
        p[i]=first+i;
        if(i==17)break;
    }
    return i;
}
static unsigned volatile_score(volatile unsigned *p)
{
    unsigned n=0;
    if(*p==10U)n+=1;
    if(*p!=10U)n+=2;
    if(!(*p==10U))n+=4;
    if(!(*p!=10U))n+=8;
    return n;
}
static void change_counter(unsigned *p) { *p+=256U; }
static int induction_edges(void)
{
    unsigned j,sum=0,visits=0;
    for(j=0;j<256U;++j)sum+=j;
    if(j!=256U || sum!=32640U)return 1;
    for(j=253U;j<255U;++j)sum^=j;
    if(j!=255U)return 2;
    for(j=0;j<3U;++j) {change_counter(&j);++visits;}
    return j!=257U || visits!=1U;
}
int main(void)
{
    static const uint32_t edge[]={0UL,1UL,0x80UL,0xffUL,0x100UL,0xffffUL,
        0x10000UL,0x12345678UL,0x80000000UL,0xaaaaaaaaUL,0x55555555UL,0xffffffffUL};
    unsigned i,j;
    unsigned char cells[16];
    volatile unsigned v;
    for(i=0;i<12;++i) {
        uint32_t x=edge[i];
        for(j=0;j<32;++j) {
            uint32_t a,b;
            runtime_count=j;
            a=x^(x<<runtime_count);
            b=(x>>runtime_count)^x;
            if(shifts[j].left(x)!=a || shifts[j].right(x)!=b)
                return puts("FAIL fused shift");
        }
        if(evolving(x,x)!=6 || evolving(x,x^1UL)!=6 || evolving(x,x^2UL)!=0)
            return puts("FAIL evolving decisions");
        observed=x;
        if((observed^(observed<<13))!=(x^(x<<13)))
            return puts("FAIL volatile snapshots");
    }
    for(i=0;i<512;++i) {
        unsigned expected=(i==257U?9:6)+((i>>8)*16U);
        if(word_score(i)!=expected) return puts("FAIL word score");
        calls=0;
        if(wrapped(i)!=(unsigned)(i+(i==10U?3:0)) || calls!=1)
            return puts("FAIL call snapshot");
        calls=0;
        if((byte_result(i)==6)!=((i&255U)==6U) || calls!=1)
            return puts("FAIL byte call promotion");
        v=i;
        if(volatile_score(&v)!=(i==10U?9:6))return puts("FAIL volatile score");
    }
    if(exact_break(cells,250)!=7 || cells[7]!=(unsigned char)257 ||
       miss_break(cells,250)!=13 || cells[12]!=(unsigned char)262)
        return puts("FAIL break counter");
    if(induction_edges())return puts("FAIL induction boundary or alias");
    puts("tbitscor passed");
    return 0;
}
