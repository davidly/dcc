/* Unsigned wide power-of-two arithmetic and upper-bit bounds. */
#include <stdint.h>
#include <stdio.h>
#define CASE(N) \
static uint32_t q##N(uint32_t x){return x/(1UL<<N);} \
static uint32_t r##N(uint32_t x){return x%(1UL<<N);} \
static unsigned b##N(uint32_t x){return (x<(1UL<<N)) + 2U*(x>=(1UL<<N)) + 4U*(x<=((1UL<<N)-1UL)) + 8U*(x>((1UL<<N)-1UL));}
CASE(0)
CASE(1)
CASE(2)
CASE(3)
CASE(4)
CASE(5)
CASE(6)
CASE(7)
CASE(8)
CASE(9)
CASE(10)
CASE(11)
CASE(12)
CASE(13)
CASE(14)
CASE(15)
CASE(16)
CASE(17)
CASE(18)
CASE(19)
CASE(20)
CASE(21)
CASE(22)
CASE(23)
CASE(24)
CASE(25)
CASE(26)
CASE(27)
CASE(28)
CASE(29)
CASE(30)
CASE(31)
struct Test {uint32_t (*q)(uint32_t);uint32_t (*r)(uint32_t);unsigned (*b)(uint32_t);};
static const struct Test tests[]={
{q0,r0,b0},
{q1,r1,b1},
{q2,r2,b2},
{q3,r3,b3},
{q4,r4,b4},
{q5,r5,b5},
{q6,r6,b6},
{q7,r7,b7},
{q8,r8,b8},
{q9,r9,b9},
{q10,r10,b10},
{q11,r11,b11},
{q12,r12,b12},
{q13,r13,b13},
{q14,r14,b14},
{q15,r15,b15},
{q16,r16,b16},
{q17,r17,b17},
{q18,r18,b18},
{q19,r19,b19},
{q20,r20,b20},
{q21,r21,b21},
{q22,r22,b22},
{q23,r23,b23},
{q24,r24,b24},
{q25,r25,b25},
{q26,r26,b26},
{q27,r27,b27},
{q28,r28,b28},
{q29,r29,b29},
{q30,r30,b30},
{q31,r31,b31}
};
static volatile uint32_t observed;
static unsigned calls;
static uint32_t once(uint32_t x){++calls;return x;}
static uint32_t mixed(uint32_t x) {
    uint32_t a=x/8UL,b=x/3UL,c=x%8UL,d=x%3UL;
    return a^b^c^d;
}
static uint32_t alias_mask(uint32_t *p) {
    uint32_t before=*p;
    *p^=0x80000001UL;
    return (before&0x80000001UL)^(*p&0x80000001UL);
}
static unsigned changed_parameter(uint32_t x) {
    unsigned n=0;
    if(x&1UL)n=1;
    x^=1UL;
    if(x&1UL)n+=2;
    return n;
}
int main(void) {
    static const uint32_t values[]={0UL,1UL,127UL,128UL,255UL,256UL,65535UL,65536UL,
        0x7fffffffUL,0x80000000UL,0xffffffffUL,0xabcdef01UL};
    unsigned i,j;
    for(i=0;i<sizeof(values)/sizeof(values[0]);++i) {
        uint32_t x=values[i],q=x,mask=0,divisor=1;
        for(j=0;j<32;++j) {
            unsigned expected=x<divisor?5U:10U;
            if(tests[j].q(x)!=q || tests[j].r(x)!=(x&mask) || tests[j].b(x)!=expected)
                return puts("FAIL wide power arithmetic");
            q>>=1;mask=(mask<<1)|1UL;divisor<<=1;
        }
        if(changed_parameter(x)!=((x&1UL)?1U:2U))return puts("FAIL modified parameter mask");
        if(alias_mask(&x)!=0x80000001UL)return puts("FAIL alias snapshot mask");
        observed=values[i];calls=0;
        if(once(observed)%256UL!=(values[i]&255UL) || calls!=1)
            return puts("FAIL power evaluation count");
    }
    if(mixed(0x00000000UL)!=0x00000000UL)return puts("FAIL mixed helpers");
    if(mixed(0x00000001UL)!=0x00000000UL)return puts("FAIL mixed helpers");
    if(mixed(0x00000007UL)!=0x00000004UL)return puts("FAIL mixed helpers");
    if(mixed(0x00000008UL)!=0x00000001UL)return puts("FAIL mixed helpers");
    if(mixed(0x00000009UL)!=0x00000003UL)return puts("FAIL mixed helpers");
    if(mixed(0x7fffffffUL)!=0x25555553UL)return puts("FAIL mixed helpers");
    if(mixed(0x80000000UL)!=0x3aaaaaa8UL)return puts("FAIL mixed helpers");
    if(mixed(0xffffffffUL)!=0x4aaaaaadUL)return puts("FAIL mixed helpers");
    if(mixed(0xabcdef01UL)!=0x2c3d18e0UL)return puts("FAIL mixed helpers");
    {volatile int32_t x=-9L;if(x/8L!=-1L || x%8L!=-1L)return puts("FAIL signed control");}
    puts("tpowwide passed");return 0;
}
