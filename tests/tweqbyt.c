/* Word equality must retain the byte not containing the small constant,
 * including signed promotion, live values, volatile reads and call results. */
#include <stdint.h>
#include <stdio.h>
#define CHECK(N,C) static unsigned f##N(uint16_t x) { unsigned n=0; \
 if(x==(C))n+=1; if(x!=(C))n+=2; if(!(x==(C)))n+=4; if(!(x!=(C)))n+=8; \
 return n+(x>>8)*16; } \
 static unsigned g##N(volatile uint16_t *p) { unsigned n=0; \
 if(*p==(C))n+=1; if(*p!=(C))n+=2; if(!(*p==(C)))n+=4; if(!(*p!=(C)))n+=8; \
 return n; }
CHECK(0,1U)
CHECK(1,2U)
CHECK(2,3U)
CHECK(3,10U)
CHECK(4,127U)
CHECK(5,128U)
CHECK(6,254U)
CHECK(7,255U)
CHECK(8,256U)
CHECK(9,512U)
CHECK(10,32512U)
CHECK(11,32768U)
CHECK(12,65024U)
CHECK(13,65280U)
CHECK(14,65534U)
CHECK(15,65535U)
struct Check { uint16_t constant; unsigned (*run)(uint16_t); unsigned (*load)(volatile uint16_t *); };
static const struct Check cases[]={
{1U,f0,g0},
{2U,f1,g1},
{3U,f2,g2},
{10U,f3,g3},
{127U,f4,g4},
{128U,f5,g5},
{254U,f6,g6},
{255U,f7,g7},
{256U,f8,g8},
{512U,f9,g9},
{32512U,f10,g10},
{32768U,f11,g11},
{65024U,f12,g12},
{65280U,f13,g13},
{65534U,f14,g14},
{65535U,f15,g15}
};
static unsigned calls;
static volatile uint16_t observed;
static uint16_t once(uint16_t x) { ++calls; return x; }
static uint16_t preserve(uint16_t x) { uint16_t v=once(x); unsigned n=0; if(v==10U)n=3; return v+n; }
static int signed_char_check(signed char c) { if(c==255)return 1; if(c==-1)return 2; return 0; }
int main(void)
{
    static const uint16_t edge[]={0,1,2,3,9,10,11,127,128,254,255,256,257,511,512,513,
        0x7fffU,0x8000U,0x80ffU,0xfe00U,0xff00U,0xff01U,0xfffeU,0xffffU};
    unsigned i,j;
    uint32_t seed=0x12345678UL;
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        for(j=0;j<152;++j) {
            uint16_t x,c=cases[i].constant;
            unsigned eq,expected;
            seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
            x=j<24?edge[j]:(uint16_t)seed;
            eq=((uint8_t)x==(uint8_t)c && (uint8_t)(x>>8)==(uint8_t)(c>>8));
            expected=(eq?9:6)+(x>>8)*16;
            observed=x;
            if(cases[i].load(&observed)!=(eq?9:6) ||
               cases[i].run(x)!=expected || preserve(x)!=(uint16_t)(x+(x==10U?3:0)))
                return puts("FAIL word equality");
        }
    }
    calls=0;
    if(once(10)!=10U || once(266)==10U || calls!=2)
        return puts("FAIL evaluation count");
    observed=0x100aU;
    if(observed==10U) return puts("FAIL high byte");
    observed=10;
    if(!(observed==10U) || signed_char_check(-1)!=2 || signed_char_check(127)!=0)
        return puts("FAIL promotion");
    puts("tweqbyt passed");
    return 0;
}
