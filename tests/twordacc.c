/* Word-loop accumulators, masked temporaries, aliases and wraparound. */
#include <stdint.h>
#include <stdio.h>
static uint16_t multiply(uint16_t a,uint16_t b){uint16_t r=0;
 while(b){if(b&1U)r+=a;a+=a;b>>=1;}return r;}
static uint16_t overwrite(uint16_t a,uint16_t b){uint16_t r=0;
 while(b){if(b&1U)r=b&1U;r+=a;a+=a;b>>=1;}return r;}
static uint16_t aliased(uint16_t a,uint16_t b){uint16_t r=0;volatile uint16_t *p=&r;
 while(b){if(b&1U)*p=(uint16_t)(*p+a);a+=a;b>>=1;}return r;}
static unsigned bounded(unsigned first,unsigned bound){unsigned i,total=0;
 for(i=first;i<bound;++i)total+=i;return total^i;}
static unsigned bounds(uint32_t x,uint32_t y){unsigned n=0;
 if(x<256UL)n+=1;if(x>=256UL)n+=2;
 if(y<0x80000000UL)n+=4;if(y>0x7fffffffUL)n+=8;return n;}
int main(void){
 static const uint16_t values[]={0U,1U,2U,3U,127U,128U,255U,256U,32767U,32768U,65535U};
 unsigned i,j;
 for(i=0;i<11;++i)for(j=0;j<11;++j){uint16_t a=values[i],b=values[j];
  uint16_t expected=(uint16_t)((uint32_t)a*(uint32_t)b);
  if(multiply(a,b)!=expected || aliased(a,b)!=expected)return puts("FAIL word cache");
 }
 if(overwrite(0U,0U)!=0U)return puts("FAIL masked scratch overwrite");
 if(overwrite(1U,1U)!=2U)return puts("FAIL masked scratch overwrite");
 if(overwrite(255U,32768U)!=32769U)return puts("FAIL masked scratch overwrite");
 if(overwrite(65535U,65535U)!=32769U)return puts("FAIL masked scratch overwrite");
 if(overwrite(127U,128U)!=16257U)return puts("FAIL masked scratch overwrite");
 if(overwrite(43981U,37U)!=31137U)return puts("FAIL masked scratch overwrite");
 if(bounded(0U,255U)!=(32385U^255U) || bounded(0U,256U)!=(32640U^256U) ||
    bounded(253U,255U)!=((253U+254U)^255U))return puts("FAIL counter boundary");
 if(bounds(0UL,0UL)!=5U || bounds(256UL,0UL)!=6U ||
    bounds(255UL,0xffffffffUL)!=9U || bounds(256UL,0x80000000UL)!=10U)
  return puts("FAIL independent wide bound keys");
 puts("twordacc passed");return 0;}
