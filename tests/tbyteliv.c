/* Compact byte loops must preserve live-out word counters and early exits. */
#include <stdio.h>
#include <stdint.h>
static uint8_t cells[256];
static int fill(uint8_t first)
{
    int i;
    for (i=249; i<255; ++i) cells[i]=first+i;
    return i;
}
static unsigned fill_unsigned(uint8_t first)
{
    unsigned i;
    for (i=3; i<13; ++i) cells[i]=first+i;
    return i+17;
}
static int check(uint8_t first)
{
    int i;
    for (i=249; i<255; ++i)
        if(cells[i]!=(uint8_t)(first+i)) return i;
    return i+1;
}
static int stop(uint8_t first)
{
    int i;
    for(i=249;i<255;++i) {
        cells[i]=first+i;
        if(i==251) break;
    }
    return i;
}
static unsigned frame(unsigned x)
{
    volatile uint8_t bytes[3];
    uint8_t *p;
    uint8_t local[2];
    bytes[0]=(uint8_t)x;
    bytes[1]=(uint8_t)(x>>8);
    bytes[2]=bytes[0]^bytes[1];
    local[0]=73; local[1]=42;
    if(local[0]!=73 || local[1]!=42) return 0;
    p=local;
    p[0]=bytes[0];p[1]=bytes[1];
    local[0]^=1;
    return local[0]+256U*local[1]+bytes[2];
}
int main(void)
{
    unsigned x,i;
    for(x=0;x<256;++x) {
        if(fill((uint8_t)x)!=255 || check((uint8_t)x)!=256)
            return puts("FAIL live counter");
        cells[252]^=1;
        if(check((uint8_t)x)!=252 || stop((uint8_t)x)!=251)
            return puts("FAIL early exit");
        if(fill_unsigned((uint8_t)x)!=30) return puts("FAIL unsigned exit");
        for(i=3;i<13;++i)
            if(cells[i]!=(uint8_t)(x+i)) return puts("FAIL bytes");
        if(frame(x*257U)!=(uint16_t)(((x^1U)+256U*x)))
            return puts("FAIL frame alias");
    }
    puts("tbyteliv passed");
    return 0;
}
