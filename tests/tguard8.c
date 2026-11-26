/* Guarded equalities preserve snapshots, signed-byte conversions, changed
 * globals, shared results, and used read/modify/write results. */
#include <stdio.h>
#include <stdint.h>
static unsigned char data[2];
static volatile unsigned char watched;
static unsigned char fold(unsigned int x)
{ return (unsigned char)((x&255U)^(x>>8)^90U); }
static unsigned int guarded(unsigned int x,unsigned char want)
{
    unsigned char saved;
    data[0]=want;data[1]=want^1U;
    if(data[0]!=fold(x)) return 1;
    if(fold(x)!=data[0]) return 2;
    if(data[1]==fold(x)) return 3;
    saved=data[0];
    if((unsigned int)saved+data[0]!=(unsigned int)want*2U) return 4;
    watched=want;
    if(watched!=fold(x)) return 5;
    data[0]^=1U;
    if(data[0]==fold(x)) return 6;
    return 0;
}
static unsigned int signed_control(unsigned char x)
{
    unsigned int r=0;
    if(x==128U) {
        if((int16_t)(signed char)x==(int16_t)x) r+=1U;
        if((int16_t)(signed char)x==-128) r+=2U;
    }
    return r;
}
static unsigned int updates(void)
{
    unsigned int old;
    data[0]=250;data[0]+=10U;data[0]^=129U;data[0]|=16U;data[0]&=223U;data[0]-=7U;
    old=data[0]++;
    return old+(unsigned int)data[0];
}
int main(void)
{
    static uint16_t values[]={0,1,127,128,255,256,257,32767,32768U,65535U};
    uint32_t sum=0;
    unsigned int i;
    for(i=0;i<10;++i) {
        unsigned char want=(unsigned char)((values[i]&255U)^(values[i]>>8)^90U);
        sum+=guarded(values[i],want);sum+=guarded(values[i],want^1U);
        sum+=signed_control((unsigned char)values[i]);
    }
    sum+=updates();
    printf("byte equalities %lu\n",(unsigned long)sum);
    return 0;
}
