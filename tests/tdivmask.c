/* Constant power-of-two division must not leave stale helper forwarding,
 * and byte-demanded shifts retain signed extension and observable reads. */
#include <stdio.h>
#include <stdint.h>
static uint16_t values[]={0,1,7,8,127,128,255,256,257,1000,32767,32768U,65534U,65535U};
static uint16_t divisors[]={3,5,255,257,1000};
static volatile uint16_t watched;
static int effects;
static uint16_t effect(void) { ++effects;return watched; }
static uint16_t mixed(uint16_t x,uint16_t divisor)
{
    uint16_t a=x/8U,b=x/divisor,c=x%256U,d=x/256U;
    uint16_t e=x%3U,f=x/32768U,g=x/2U,h=x/1000U;
    return (uint16_t)(a+b+c+d+e+f+g+h);
}
static uint16_t shared(uint16_t x)
{
    uint16_t k=256;
    return (uint16_t)((x/k)+(x%k)+(x/3U)+(x/8U)+(x/1000U));
}
static uint16_t signed_control(int16_t x)
{ return (uint16_t)(x/2+x/8+x/256+x%8); }
static unsigned char truncated(uint16_t x)
{ return (unsigned char)(((unsigned char)x)>>8); }
static signed char signed_shift(signed char x)
{ return (signed char)(x>>8); }
int main(void)
{
    uint32_t sum=0;
    unsigned int i,j;
    for(i=0;i<14;++i) {
        for(j=0;j<5;++j) sum+=mixed(values[i],divisors[j]);
        sum+=shared(values[i]);sum+=signed_control((int16_t)values[i]);
        if(truncated(values[i])!=0) return 1;
    }
    watched=65535U;effects=0;
    if(truncated(effect())!=0 || effects!=1 || signed_shift(-128)!=-1 || signed_shift(127)!=0) return 1;
    printf("division masks %lu\n",(unsigned long)sum);
    return 0;
}
