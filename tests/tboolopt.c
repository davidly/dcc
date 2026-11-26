/* Callee-global binding and one-trip-loop proof boundaries. */
#include <stdio.h>
static volatile int state;
static int plain;
static int yes(void) { if(state<20) return 1; return 0; }
static int nonvolatile(void) { if(plain<20) return 1; return 0; }
static void set(int x) { state=x; plain=x; }
static int local_shadow(void)
{
    long state=70000L;
    unsigned char plain=99;
    int a=yes()+nonvolatile();
    set(30);
    return 100*a+yes()+nonvolatile()+(state==70000L)+(plain==99);
}
static int nested_shadow(void)
{
    int n=yes();
    { enum { state=99 }; typedef long plain;
      n+=yes()+nonvolatile()+99+(sizeof(plain)==4); }
    return n+yes();
}
static int *escaped;
static void escape_change(void) { *escaped=2; }
static int escaped_loop(void)
{
    int i;
    escaped=&i;
    for(i=0;i<1;++i) { escape_change(); }
    return i!=3;
}
static int loops(int x)
{
    int i,n=0;
    for(i=-4;i<-3;++i) { n+=i+10; }
    if(i!=-3 || n!=6) return 1;
    for(i=0;i<1;++i) { ++n; if(x) continue; n+=2; }
    if(i!=1 || n!=(x?7:9)) return 2;
    for(i=0;i<1;++i) { ++n; break; }
    if(i!=0) return 3;
    for(i=0;i<1;++i) { i=3; }
    if(i!=4) return 4;
    for(i=0;i<1;++i) { int j=(i=5); n+=j; }
    if(i!=6) return 5;
    /* Conversion to unsigned makes the first condition false. */
    for(i=-1;i<0U;++i) { return 7; }
    if(i!=-1) return 8;
    for(i=0;i<0;++i) { return 9; }
    for(i=0;i<1;++i) { volatile int j=1; i+=j; }
    if(i!=2) return 10;
    /* External entry into the body must retain the real loop control. */
    i=4;
    goto inside;
    for(i=0;i<1;++i) { inside: n+=1; }
    if(i!=5) return 11;
    { volatile int v; n=0; for(v=0;v<1;++v) ++n;
      if(v!=1 || n!=1) return 12; }
    return 0;
}
static int switch_entry(void)
{
    int i=4,n=0;
    switch(0) {
        for(i=0;i<1;++i) { case 0: ++n; }
    }
    return i!=5 || n!=1;
}
static int mul(int x)
{
    return 3*(x<20)+7*(x>=20)+255*(x==20)+(-3)*(x!=20);
}
int main(void)
{
    int i;
    int (*callback)(void)=yes;
    for(i=-1;i<40;++i) {
        int a=i<20;
        set(i);
        { int r=local_shadow(); if(r!=200*a+2) { printf("FAIL local %d %d\n",i,r); return 1; } }
        set(i);
        { int r=nested_shadow(); if(r!=100+4*a || callback()!=a) { printf("FAIL nested %d %d\n",i,r); return 2; } }
        if(mul(i)!=3*a+7*(i>=20)+255*(i==20)-3*(i!=20)) return 3;
    }
    { int a=loops(0), b=loops(1), c=escaped_loop()+switch_entry();
      if(a || b || c) { printf("FAIL loops %d %d %d\n",a,b,c); return 4; } }
    puts("tboolopt passed");
    return 0;
}
