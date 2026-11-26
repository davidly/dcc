/* Loop-carried long decrements and 0/1 additions must retain all carries,
 * borrows, live old values, zero iterations, and volatile updates. */
#include <stdio.h>
#include <stdint.h>
static uint32_t seeds[]={0UL,1UL,255UL,256UL,65535UL,65536UL,16777216UL,2147483648UL,4294967295UL};
static uint32_t decremented[]={4294967293UL,4294967294UL,252UL,253UL,65532UL,65533UL,16777213UL,2147483645UL,4294967292UL};
static uint32_t added[]={2UL,3UL,257UL,258UL,65537UL,65538UL,16777218UL,2147483650UL,1UL};
static volatile uint32_t observable;
static uint32_t down(uint32_t seed,int count)
{
    uint32_t value=seed;
    int i;
    for(i=0;i<count;++i) {
        if(value==0UL) observable=0UL;
        --value;
    }
    return value;
}
static uint32_t sum(uint32_t seed)
{
    uint32_t value=seed;
    int i;
    for(i=0;i<4;++i) value+=(i<2);
    return value;
}
static uint32_t live(uint32_t seed)
{
    uint32_t value=seed,old,total=0;
    int i;
    for(i=0;i<3;++i) { old=value--; total+=old; }
    return total;
}
static uint32_t negative_control(uint32_t seed)
{
    uint32_t value=seed;
    int i;
    for(i=0;i<4;++i) value+=(i<2 ? -1 : 2);
    return value;
}
int main(void)
{
    int i,j;
    for(i=0;i<9;++i) {
        if(down(seeds[i],3)!=decremented[i] || down(seeds[i],0)!=seeds[i] ||
           sum(seeds[i])!=added[i] || negative_control(seeds[i])!=added[i] ||
           live(seeds[i])!=seeds[i]*3UL-3UL) {
            puts("FAIL long PHI"); return 1;
        }
        observable=seeds[i];
        for(j=0;j<3;++j) --observable;
        if(observable!=decremented[i]) { puts("FAIL volatile"); return 1; }
    }
    puts("tlongphi passed"); return 0;
}
