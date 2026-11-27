/* Nested array arguments and pooled inline snapshots retain conversions,
 * byte overflow, alias effects, and single evaluation of call arguments. */
#include <stdint.h>
#include <stdio.h>
static unsigned calls;
static int equal(const uint8_t *p, uint8_t a, uint8_t b)
{
    if (*p != a+b) return 0;
    return 1;
}
static int different(const uint8_t *p, uint8_t a, uint8_t b)
{
    if (*p == a+b) return 0;
    return 1;
}
static uint8_t once(uint8_t x) { ++calls; return x; }
static void change(uint8_t *p) { *p=99; }
static int volatile_equal(const volatile uint8_t *p, uint8_t a, uint8_t b)
{
    if (*p != a+b) return 0;
    return 1;
}
static unsigned word(uint16_t x) { ++calls; return x; }
int main(void)
{
    static const unsigned inputs[]={0,1,127,128,254,255};
    unsigned i,j;
    for(i=0;i<256;++i)
        for(j=0;j<6;++j) {
            unsigned b=inputs[j];
            uint8_t bytes[2];
            bytes[0]=(uint8_t)(i+b);
            bytes[1]=73;
            if(equal(bytes,(uint8_t)i,(uint8_t)b)!=(i+b<=255) ||
               different(bytes,(uint8_t)i,(uint8_t)b)!=(i+b>255))
                return puts("FAIL promoted sum");
            bytes[0]^=1;
            if(equal(bytes,(uint8_t)i,(uint8_t)b)) return puts("FAIL fresh byte");
            change(bytes);
            if(!equal(bytes,99,0) || !volatile_equal(bytes,99,0) || bytes[1]!=73)
                return puts("FAIL alias or literal");
            calls=0;
            if(!equal(bytes,once(99),once(0)) || calls!=2)
                return puts("FAIL argument evaluation");
            {
                uint8_t bytes[1];
                bytes[0]=255;
                if(!equal(bytes,255,0) || equal(bytes,255,1))
                    return puts("FAIL shadowed array");
            }
            if(!equal(bytes,99,0)) return puts("FAIL pooled slot");
        }
    calls=0;
    if(word(257)==1 || word(258)==2 || word(65534U)!=65534U ||
       word(65535U)!=65535U || calls!=4) return puts("FAIL call word equality");
    puts("tbytinl passed");
    return 0;
}
