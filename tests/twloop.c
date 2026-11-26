/* Tiny long loops: wrap, old values, exits, counter writes and escapes. */
#include <stdio.h>
#include <stdint.h>
typedef uint32_t u32;
static volatile u32 observed;
static int *escaped;
static void change(void) { *escaped=4; }
static u32 carry(u32 v) { int i; for(i=0;i<4;++i) ++v; return v; }
static u32 borrow(u32 v) { int i; for(i=0;i<3;++i) --v; return v; }
static u32 live(u32 v) { u32 n=0,old; int i; for(i=0;i<3;++i) { old=v--; n+=old; } return n; }
static u32 skip(u32 v) { int i; for(i=0;i<4;++i) { if(i==1) continue; ++v; } return v; }
static u32 stop(u32 v) { int i; for(i=0;i<4;++i) { if(i==2) break; ++v; } return v; }
static u32 early(u32 v) { int i; for(i=0;i<3;++i) { if(i==1) return v; --v; } return 0; }
static u32 changed_counter(u32 v) { int i; for(i=0;i<3;++i) { ++v; ++i; } return v; }
static u32 escaped_counter(u32 v) { int i; escaped=&i; for(i=0;i<3;++i) { ++v; change(); } return v; }
static u32 old_exit(u32 v) { int i; u32 old; for(i=0;i<3;++i) { old=v--; if(i==1) return old; } return 0; }
static u32 zero(u32 v,int count) { int i; for(i=0;i<count;++i) --v; return v; }
static u32 nested_local(u32 v) { int i; for(i=0;i<3;++i) { u32 saved=v; --v; observed=saved; } return v; }
static u32 cases(u32 v) { int i=5; switch(0) { for(i=0;i<3;++i) { case 0: ++v; } } return v+(i==6); }
static u32 values[]={0UL,1UL,255UL,256UL,65535UL,65536UL,16777215UL,16777216UL,0x7fffffffUL,0x80000000UL,0xffffffffUL};
int main(void)
{
    int i,j; u32 v;
    for(i=0;i<11;++i) {
        v=values[i];
        if(carry(v)!=v+4UL || borrow(v)!=v-3UL || live(v)!=v*3UL-3UL ||
           skip(v)!=v+3UL || stop(v)!=v+2UL || early(v)!=v-1UL ||
           changed_counter(v)!=v+2UL || escaped_counter(v)!=v+1UL ||
           old_exit(v)!=v-1UL || zero(v,0)!=v || zero(v,3)!=v-3UL ||
           nested_local(v)!=v-3UL || cases(v)!=v+2UL) {
            printf("FAIL loop %d\n",i);return 1;
        }
        observed=v;
        for(j=0;j<3;++j) --observed;
        if(observed!=v-3UL) return 2;
    }
    puts("twloop passed");return 0;
}
