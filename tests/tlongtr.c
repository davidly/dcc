/* Direct long truth loads and widening 0/1 PHIs must preserve all bits,
 * live registers, volatile accesses, and pre-call snapshots. */
#include <stdio.h>
static long g;
static volatile long vg;
static long values[7]={0L,1L,256L,65536L,16777216L,-2147483647L-1L,-1L};
static void clear(void) { g=0; }
static int choose(int x) { if(g) x+=7; return x; }
static int inverse(void) { return !g; }
static long boolean_long(int x,int y) { return x && y; }
static long not_boolean(int x) { return (long)(x ? -1 : 2); }
static int snapshot(void) { long saved=g; clear(); return saved ? 7:9; }
static int local(long v) { long x=v; long *p=&x; *p=v; return x ? 7:9; }
int main(void)
{
    int i;
    for(i=0;i<7;++i) {
        int want=i==0?0:1;
        g=values[i]; vg=values[i];
        if(choose(13)!=(want?20:13) || inverse()!=!want ||
           local(values[i])!=(want?7:9) || (vg?1:0)!=want ||
           snapshot()!=(want?7:9)) { puts("FAIL truth"); return 1; }
    }
    if(boolean_long(0,7)!=0L || boolean_long(-5,7)!=1L ||
       not_boolean(1)!=-1L || not_boolean(0)!=2L) { puts("FAIL widening"); return 1; }
    puts("tlongtr passed"); return 0;
}
