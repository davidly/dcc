/* Independent bounded byte-data loops; surrounding word uses, aliases,
 * counter writes, wide bounds and jumps must retain their original meaning. */
#include <stdio.h>
static unsigned char data[256];
static unsigned int good(int branch)
{
    unsigned int total=0;
    unsigned int i;
    for(i=0;i<32U;++i) data[i]=(unsigned char)(i+128U);
    for(i=0;i<32U;++i) if(data[i]!=(unsigned char)(i+128U)) return 999U;
    if(branch) {
        for(i=0;i<16U;++i) total+=(unsigned char)i;
    } else {
        for(i=0;i<8U;++i) total+=(unsigned char)i;
    }
    return total;
}
static unsigned int outside(void)
{
    unsigned int total=0;
    unsigned int i;
    for(i=0;i<8U;++i) total+=(unsigned char)i;
    i=300U; total+=i;
    for(i=0;i<8U;++i) total+=(unsigned char)i;
    return total;
}
static unsigned int wide(void)
{
    unsigned int total=0;
    unsigned int i;
    for(i=0;i<256U;++i) total+=(unsigned char)i;
    for(i=0;i<2U;++i) total+=(unsigned char)i;
    return total;
}
static unsigned int modified(void)
{
    unsigned int total=0;
    unsigned int i;
    for(i=0;i<8U;++i) { total+=(unsigned char)i; ++i; }
    for(i=0;i<8U;++i) total+=(unsigned char)i;
    return total;
}
static unsigned int escaped(void)
{
    unsigned int total=0,*p;
    unsigned int i;
    p=&i;
    for(i=0;i<8U;++i) { total+=(unsigned char)i; *p+=1U; }
    for(i=0;i<8U;++i) total+=(unsigned char)i;
    return total;
}
static unsigned int jumped(void)
{
    unsigned int total=0;
    unsigned int i;
    i=300U; goto body;
    for(i=0;i<8U;++i) {
body: total+=(unsigned char)i;
    }
    for(i=0;i<8U;++i) total+=(unsigned char)i;
    return total;
}
int main(void)
{
    if(good(1)!=120U || good(0)!=28U || outside()!=356U || wide()!=32641U ||
       modified()!=40U || escaped()!=40U || jumped()!=72U) {
        puts("FAIL independent byte loops"); return 1;
    }
    puts("tforseq passed"); return 0;
}
