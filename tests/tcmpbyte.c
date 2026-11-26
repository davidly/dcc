/* All signed 16-bit values against positive byte constants. The oracle
 * uses unsigned bit ranges, independently of the signed comparison path. */
#include <stdio.h>
#include <stdint.h>
volatile int16_t value;
int lt1(void) { if(value<1) return 1; return 0; }
int lt20(void) { if(value<20) return 1; return 0; }
int lt128(void) { if(value<128) return 1; return 0; }
int lt255(void) { if(value<255) return 1; return 0; }
int ge20(void) { if(value>=20) return 1; return 0; }
int notlt(void) { if(!(value<20)) return 1; return 0; }
int loop(void) { int i,s; s=0; for(i=0;i<20;++i) s+=i; return s; }
int main(void)
{
    uint16_t bits;
    unsigned long errors;
    int e;
    bits=0; errors=0;
    do {
        value=(int16_t)bits;
        if(lt1()!=(bits>=32768u || bits<1u)) errors++;
        e=(bits>=32768u || bits<20u);
        if(lt20()!=e || ge20()==e || notlt()==e) errors++;
        if(lt128()!=(bits>=32768u || bits<128u)) errors++;
        if(lt255()!=(bits>=32768u || bits<255u)) errors++;
        bits++;
    } while(bits!=0);
    printf("values=65536 errors=%lu loop=%d\n",errors,loop());
    return errors!=0;
}
