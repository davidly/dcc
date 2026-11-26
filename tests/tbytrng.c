/* A byte's natural period bounds a synchronized word counter only when
 * initialization, unit steps, and backedges match. Guards in the controls
 * must remain observable. */
#include <stdio.h>
static unsigned int bounded(void)
{
    unsigned char i=0;
    unsigned int count=0;
    do { if(++count>256U) return 999U; } while(++i);
    return count;
}
static unsigned int down(void)
{
    unsigned char i=0;
    unsigned int count=0;
    do { if(++count>256U) return 999U; } while(--i);
    return count;
}
static unsigned int initialized(void)
{
    unsigned char i=0;
    unsigned int count=1;
    do { if(++count>256U) return 999U; } while(++i);
    return count;
}
static unsigned int stride(void)
{
    unsigned char i=0;
    unsigned int count=0;
    do { count+=2U; if(count>256U) return 999U; } while(++i);
    return count;
}
static unsigned int nested(void)
{
    unsigned char i=0;
    unsigned int count=0;
    int j;
    do {
        for(j=0;j<2;++j) ++count;
        if(count>256U) return 999U;
    } while(++i);
    return count;
}
static unsigned int reset(void)
{
    unsigned char i=0;
    unsigned int count=0;
    do { i=0; if(++count>256U) return 999U; } while(++i);
    return count;
}
static unsigned int observed(void)
{
    volatile unsigned char i=0;
    unsigned int count=0;
    do { if(++count>256U) return 999U; } while(++i);
    return count;
}
static unsigned int early(void)
{
    unsigned char i=0;
    unsigned int count=0;
    do { if(++count<=256U) return 17U; } while(++i);
    return count;
}
int main(void)
{
    if(bounded()!=256U || down()!=256U || initialized()!=999U || stride()!=999U ||
       nested()!=999U || reset()!=999U || observed()!=256U || early()!=17U) {
        puts("FAIL byte range"); return 1;
    }
    puts("tbytrng passed"); return 0;
}
