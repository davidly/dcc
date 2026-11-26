/* Invariant local pointers may rematerialize across calls and target stores;
 * reassignment, address escape, volatile pointers, and conditional initializers
 * must retain their normal semantics. */
#include <stdio.h>
static unsigned char data[64];
static unsigned int calls;
static void touch(void) { ++calls; data[63]=7; }
static void redirect(unsigned char **p) { *p=data+32; }
static int fixed(void)
{
    unsigned char *a=data, *b=data+32;
    int i;
    for(i=0;i<32;++i) {
        a[i]=(unsigned char)i; touch(); b[i]=(unsigned char)(i+32);
        if(a[i]!=i || b[i]!=i+32) return 1;
    }
    return 0;
}
static int changed(int choose)
{
    unsigned char *p=data;
    unsigned char * volatile vp=data;
    int i;
    for(i=0;i<2;++i) {
        if(i) { p=data+32; vp=data+32; }
        if(*p!=i*32 || *vp!=i*32) return 1;
    }
    p=data; redirect(&p);
    if(*p!=32) return 2;
    if(choose) p=data; else p=data+32;
    for(i=0;i<2;++i) if(*p!=(choose?0:32)) return 3;
    { unsigned char *p=data+1; for(i=0;i<2;++i) if(*p!=1) return 4; }
    return *p!=(choose?0:32);
}
static int conditional(int choose)
{
    unsigned char *p;
    int i;
    if(choose) p=data; else p=data+32;
    for(i=0;i<4;++i) if(p[i]!=i+(choose?0:32)) return 1;
    return 0;
}
int main(void)
{
    if(fixed() || changed(0) || changed(1) || conditional(0) || conditional(1) || calls!=32) {
        puts("FAIL fixed pointer"); return 1;
    }
    puts("tfixptr passed"); return 0;
}
