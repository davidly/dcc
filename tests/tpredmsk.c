/* Predicate masks and dominated facts must hold for independent arguments,
 * including out-of-byte-range values, overlapping bits and overwrites. */
#include <stdio.h>
#include <stdint.h>
static uint16_t values[]={0,1,63,64,127,128,254,255,256,257,16383,16384,32767,32768U,65279U,65280U,65535U};
static unsigned int score(unsigned int x)
{
    unsigned int r=0;
    if(x<256U) r|=1U;
    if(x>=16384U) r|=2U;
    if(!(x>=65280U)) r|=4U;
    return r;
}
static int check(unsigned int x,unsigned int y)
{
    if(score(x)!=((y<1U?1U:0U)|(y>=64U?2U:0U)|(y<255U?4U:0U))) return 4;
    if((x<256U)!=(y<1U) || (x>=256U)!=(y>=1U) ||
       (x<16384U)!=(y<64U) || (x>=65280U)!=(y>=255U)) return 1;
    if((x<257U)!=(y==0U || x==256U)) return 2;
    if(((int16_t)x<256)!=(y>=128U || y==0U)) return 3;
    return 0;
}
static unsigned int boundary(unsigned int x)
{ return (x<1U?1U:0U)|(x>=64U?2U:0U)|(x==255U?4U:0U)|(x>255U?8U:0U); }
static unsigned int overlap(unsigned int x)
{
    unsigned int r=0;
    if(x<256U) r|=1U;
    if(x>=16384U) r|=1U;
    return r;
}
static unsigned int overwrite(unsigned int x)
{
    unsigned int r=0;
    if(x<256U) r=1U;
    if(x>=128U) r=2U;
    return r;
}
static unsigned int score8(unsigned int x)
{
    unsigned int r=0;
    if(x<1U) r|=1U;
    if(x>=2U) r|=2U;
    if(x<64U) r|=4U;
    if(x>=128U) r|=8U;
    if(x<256U) r|=16U;
    if(x>=16384U) r|=32U;
    if(x<32768U) r|=64U;
    if(x>=65280U) r|=128U;
    return r;
}
static int check8(unsigned int x,unsigned int y)
{
    if(score8(x)!=score8(y)) return 1;
    if((x<1U)!=(y<1U) || (x>=2U)!=(y>=2U) || (x<64U)!=(y<64U) ||
       (x>=128U)!=(y>=128U) || (x<256U)!=(y<256U) ||
       (x>=16384U)!=(y>=16384U) || (x<32768U)!=(y<32768U) ||
       (x>=65280U)!=(y>=65280U)) return 2;
    return 0;
}
static int not_boolean(unsigned int x,unsigned int y)
{
    if(x==y) return (x==!y)!=(x==y);
    return (x==!y)==(x==y);
}
int main(void)
{
    uint32_t sum=0;
    unsigned int i,j;
    for(i=0;i<17;++i) {
        for(j=0;j<17;++j) {
            sum+=(unsigned int)check(values[i],values[j]);
            sum+=(unsigned int)check8(values[i],values[j]);
        }
        sum+=boundary(values[i])+overlap(values[i])+overwrite(values[i]);
        for(j=0;j<17;++j) sum+=(unsigned int)not_boolean(values[i],values[j]);
    }
    printf("predicate masks %lu\n",(unsigned long)sum);
    return 0;
}
