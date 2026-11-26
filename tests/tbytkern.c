/* Invariant parameter arithmetic, promoted byte ordering, and byte-memory
 * comparisons. Controls retain writes, aliasing, volatile reads and shared
 * expressions that cannot safely be bypassed. */
#include <stdio.h>
#include <stdint.h>
static volatile unsigned char observed;
static uint32_t kernel(unsigned char n,uint32_t e,uint32_t g)
{
    do { e+=g+1UL; } while(--n);
    return e;
}
static uint32_t changed(unsigned char n,uint32_t e,uint32_t g)
{
    do { e+=g+1UL; ++g; } while(--n);
    return e;
}
static uint32_t aliased(unsigned char n,uint32_t e,uint32_t g)
{
    uint32_t *p=&g;
    do { e+=g+1UL; ++*p; } while(--n);
    return e;
}
static uint32_t entered(unsigned char n,uint32_t e,uint32_t g)
{
    if(n==2) goto body;
    do {
        e+=g+1UL;
body:   --n;
    } while(n);
    return e;
}
static uint32_t volatile_parameter(unsigned char n,uint32_t e,volatile uint32_t g)
{
    do { e+=g+1UL; } while(--n);
    return e;
}
static int ordering(unsigned char a,unsigned char b)
{
    return (a<b)+2*(a<=b)+4*(a>b)+8*(a>=b)+16*(a==b)+32*(a!=b);
}
static unsigned char bytes[]={0,127,128,255};
static unsigned int check(unsigned char *p,unsigned int x)
{
    unsigned int out=0,shared=x+128U;
    if(*p==(unsigned char)(x+128U)) out|=1U;
    if(*p!=(unsigned char)(128U+x)) out|=2U;
    if(*p==(unsigned char)(x-127U)) out|=4U;
    if(*p==(unsigned char)(x^255U)) out|=8U;
    if(*p==(unsigned char)(x|128U)) out|=16U;
    if(*p==(unsigned char)(x&255U)) out|=32U;
    if(*p==(unsigned char)shared) out|=64U;
    observed=*p;
    if(observed==(unsigned char)(x+128U)) out|=128U;
    return out+shared;
}
int main(void)
{
    unsigned int a,b;
    uint16_t sum=0;
    if(kernel(3,7UL,0xffffffffUL)!=7UL ||
       kernel(3,7UL,0x7fffffffUL)!=0x80000007UL ||
       kernel(3,7UL,65535UL)!=196615UL ||
       changed(3,7UL,0xffffffffUL)!=10UL ||
       aliased(3,7UL,0xffffffffUL)!=10UL || entered(2,7UL,9UL)!=17UL ||
       volatile_parameter(3,7UL,9UL)!=37UL) return 1;
    for(a=0;a<4;++a) for(b=0;b<4;++b)
        sum+=ordering(bytes[a],bytes[b]);
    printf("byte ordering %u\n",(unsigned int)sum);
    sum=0;
    for(a=0;a<4;++a) {
        sum+=check(&bytes[a],0U);
        sum+=check(&bytes[a],127U);
        sum+=check(&bytes[a],128U);
        sum+=check(&bytes[a],255U);
        sum+=check(&bytes[a],65535U);
    }
    printf("byte kernels %u\n",(unsigned int)sum);
    return 0;
}
