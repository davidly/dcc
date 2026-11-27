/* Shared wide predicates and immediate comparisons must retain snapshots,
 * volatile accesses, control-flow dominance and all high bytes. */
#include <stdint.h>
#include <stdio.h>
static volatile uint32_t observed;
static int repeated(uint32_t a,uint32_t b,int choose)
{
    int n=0;
    if(a==b) n+=1;
    if(b!=a) n+=2;
    if(!(a==b)) n+=4;
    if(!(b!=a)) n+=8;
    if(choose) { if(a==b) n+=16; } else { if(b!=a) n+=32; }
    return n;
}
static int conditional(uint32_t a,uint32_t b,int choose)
{
    int n=0;
    if(choose) n=(a==b);
    if(a==b) n+=2;
    return n;
}
static int alias(uint32_t *p,uint32_t b)
{
    int n=(*p==b);
    *p ^= 1UL;
    return n+2*(*p==b);
}
static int vol(uint32_t b)
{
    int n=(observed==b);
    observed=b^1UL;
    return n+2*(observed==b);
}
static int changing(uint32_t a,uint32_t b)
{
    int i,n=0;
    for(i=0;i<4;++i) {
        if(a==b) n+=1;
        a ^= 1UL;
        if(a==b) n+=2;
    }
    return n;
}
static unsigned bounds(uint32_t x)
{
    return (x<=255UL)+2*(x<256UL)+4*(x>255UL)+8*(x>=256UL)+
        16*(x<=65535UL)+32*(x<65536UL)+64*(x>65535UL)+128*(x>=65536UL)+
        256*(x<=16777215UL)+512*(x<16777216UL)+1024*(x>16777215UL)+2048*(x>=16777216UL);
}
static int branch_bounds(uint32_t x)
{
    int n=0;
    if(!(x>65535UL)) ++n;
    if((x<65536UL)==0) n+=2;
    if(x>=16777216UL) n+=4;
    if(x<=255UL) n+=8;
    return n;
}
static int constants(uint32_t x)
{
    return (x==0x12345678UL)+2*(x!=0x12345678UL)+4*(0x12345678UL==x)+8*(0x12345678UL!=x);
}
static int signed_bounds(int32_t x)
{
    return (x<65536L)+2*(x<=65535L)+4*(x>255L)+8*(x>=256L);
}
int main(void)
{
    static const uint32_t edge[]={0UL,1UL,254UL,255UL,256UL,257UL,65534UL,65535UL,
        65536UL,65537UL,0x00fffffeUL,0x00ffffffUL,0x01000000UL,0x12345678UL,
        0x7fffffffUL,0x80000000UL,0xfffffffeUL,0xffffffffUL};
    unsigned i,j,k;
    for(i=0;i<sizeof(edge)/sizeof(edge[0]);++i) {
        uint32_t x=edge[i];
        uint16_t lo=(uint16_t)x,hi=(uint16_t)(x>>16);
        int fit8=(hi==0 && lo<256),fit16=(hi==0),fit24=(hi<256);
        unsigned expected=(fit8?3:12)+(fit16?48:192)+(fit24?768:3072);
        if(bounds(x)!=expected || branch_bounds(x)!=(fit16?1:2)+(fit24?0:4)+(fit8?8:0)) {
            printf("FAIL unsigned bounds i=%u got=%u expected=%u branch=%d\n",i,bounds(x),expected,branch_bounds(x));
            return 1;
        }
        if(constants(x)!=((hi==0x1234 && lo==0x5678)?5:10))
            return puts("FAIL immediate equality");
        observed=x;
        if(vol(x)!=1) return puts("FAIL volatile snapshot");
        for(j=0;j<sizeof(edge)/sizeof(edge[0]);++j) {
            uint32_t y=edge[j];
            int eq=(lo==(uint16_t)y && hi==(uint16_t)(y>>16));
            for(k=0;k<2;++k) {
                if(repeated(x,y,k)!=(eq?9:6)+(k?(eq?16:0):(eq?0:32)) ||
                    conditional(x,y,k)!=(eq?(k?3:2):0))
                    return puts("FAIL shared predicate");
            }
        }
        if(alias(&x,edge[i])!=1 || alias(&x,edge[i])!=2)
            return puts("FAIL alias snapshot");
    }
    {
        uint32_t seed=0x31415926UL;
        for(i=0;i<128;++i) {
            uint32_t a,b;
            int eq;
            seed ^= seed<<13; seed ^= seed>>17; seed ^= seed<<5;
            a=seed; b=i&1 ? a : a^0x10000UL; eq=(i&1)!=0;
            if(repeated(a,b,i&2)!=(eq?9:6)+((i&2)?(eq?16:0):(eq?0:32)) ||
                conditional(a,b,i&2)!=(eq?((i&2)?3:2):0) || changing(a,a)!=6)
                return puts("FAIL evolving predicate");
        }
    }
    if(signed_bounds(-1L)!=3 || signed_bounds(-2147483647L)!=3 ||
        signed_bounds(65535L)!=15 || signed_bounds(65536L)!=12)
        return puts("FAIL signed bounds");
    puts("twcmp passed");
    return 0;
}
