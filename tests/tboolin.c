/* Tiny boolean leaves can inline without losing global binding, volatile
 * accesses, short circuiting, or the retained function-pointer body. */
#include <stdio.h>
static volatile int value;
static unsigned int calls;
static int leaf(void) { if(value<20) return 1; return 0; }
static int inverse(void) { if(!(value<20)) return 1; return 0; }
static int other(void) { if(value<20) return 2; return 0; }
static void change(int x) { ++calls; value=x; }
static int shadow(void)
{
    int value=99;
    return leaf()+value;
}
static int nested(void)
{
    int r=leaf();
    { int value=77; r+=leaf()+value; }
    return r+leaf();
}
static int enum_shadow(void)
{
    int i,r=0;
    for(i=0;i<1;++i) {
        r=leaf();
        { enum { value=77 }; r+=leaf()+77; }
        r+=leaf();
    }
    return r;
}
static int type_shadow(void)
{
    int i,r=0;
    for(i=0;i<1;++i) {
        r=leaf();
        { typedef int value; r+=leaf()+sizeof(value); }
        r+=leaf();
    }
    return r;
}
static unsigned char rollover(unsigned char x)
{
    return ++x;
}
int main(void)
{
    int i;
    int (*callback)(void)=leaf;
    for(i=-1;i<260;++i) {
        change(i);
        if(leaf()!=(i<20) || inverse()!=(i>=20) || callback()!=(i<20) ||
           other()!=(i<20?2:0) || shadow()!=99+(i<20) ||
           nested()!=77+3*(i<20) || enum_shadow()!=77+3*(i<20) ||
           type_shadow()!=2+3*(i<20)) { printf("FAIL leaf %d: %d %d %d %d %d %d %d %d\n",i,leaf(),inverse(),callback(),other(),shadow(),nested(),enum_shadow(),type_shadow()); return 1; }
    }
    change(19);
    if(!(leaf() && (change(20),inverse())) || calls!=263U ||
       rollover(255)!=0 || rollover(0)!=1) { puts("FAIL effects"); return 1; }
    puts("tboolin passed"); return 0;
}
