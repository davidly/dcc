/* Loop counter guards keep argument evaluation and increment polarity. */
#include <stdio.h>
static unsigned count, evaluations;
static void zero(unsigned value) { if(!value)++count; }
static void nonzero(unsigned value) { if(value)++count; }
static unsigned once(unsigned value) { ++evaluations;return value; }
static unsigned result(unsigned value) { if(!value)++count;return value+7U; }
int main(void) {
    unsigned i;
    void (*p)(unsigned)=zero;
    for(i=0;i<512U;++i) {
        zero(once(i&3U));
        nonzero(i&3U);
    }
    if(count!=512U || evaluations!=512U)return puts("FAIL guarded loop counts");
    p(0U);
    nonzero(1U);
    if(count!=514U)return puts("FAIL ordinary and indirect guard calls");
    for(i=0;i<16U;++i)if(result(i)!=i+7U)return puts("FAIL nonvoid guard result");
    if(count!=515U)return puts("FAIL nonvoid guard effects");
    puts("tguardct passed");return 0;
}
