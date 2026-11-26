/* Exhaustive immediate word comparisons, with live values spanning each
 * comparison, signedness-changing casts, and volatile snapshot controls. */
#include <stdio.h>
#include <stdint.h>
static volatile uint16_t observed;
static int check(uint16_t x, unsigned int high, unsigned int low)
{
    unsigned int keep=x^0x5aa5U;
    int neg=high>=128U;
    if ((x<1U)!=(high==0U && low==0U)) return 1;
    if ((x>=255U)!=(high!=0U || low==255U)) return 2;
    if ((x>256U)!=(high>1U || (high==1U && low!=0U))) return 3;
    if ((x<=256U)!=(high==0U || (high==1U && low==0U))) return 4;
    if ((x<257U)!=(high==0U || (high==1U && low==0U))) return 5;
    if ((x>=32768U)!=neg || (x>65535U)!=0 || (x<=65535U)!=1) return 6;
    if (((int)x<0)!=neg || ((int)x>=0)==neg) return 7;
    if (((int)x < -1)!=(neg && x!=65535U)) return 8;
    if (((int)x >= -32768)!=1 || ((int)x>32767)!=0) return 9;
    if (((int)x<=-32768)!=(x==32768U)) return 10;
    if (((int)x>255)!=(high>0U && high<128U)) return 11;
    if ((x==0U)!=(high==0U && low==0U) ||
        (x!=0xff00U)!=(high!=255U || low!=0U)) return 12;
    if (keep!=(x^0x5aa5U)) return 13;
    observed=x;
    if ((observed<257U)!=(high==0U || (high==1U && low==0U))) return 14;
    return 0;
}
static int snapshot(void)
{
    uint16_t saved;
    observed=256U; saved=observed; observed=65535U;
    return saved<=256U && observed>256U;
}
int main(void)
{
    uint16_t x=0;
    unsigned int high=0,low=0;
    do {
        int code=check(x,high,low);
        if(code) { printf("FAIL %d at %u\n",code,x); return 1; }
        ++x; ++low; if(low==256U) { low=0; ++high; }
    } while(x);
    if(!snapshot()) { puts("FAIL snapshot"); return 1; }
    puts("timhome passed"); return 0;
}
