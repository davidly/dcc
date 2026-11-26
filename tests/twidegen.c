/* Unsigned rotate canonicalization: every legal constant count, both OR
 * operand orders, and independent literal oracles. Also check global
 * carry/borrow updates and negative fusion cases with aliases/calls. */
#include <stdio.h>
static unsigned long g;
static volatile unsigned long vg;
static void bump(void) { g = 7UL; }
static void update(unsigned int n) { while(n--) ++g; }
static void decrease(unsigned int n) { while(n--) --g; }
static unsigned long alias(unsigned long *p, unsigned int n) { while(n--) { unsigned long t=g; *p=7UL; g=t+1UL; } return g; }
static unsigned long call(unsigned int n) { while(n--) { unsigned long t=g; bump(); g=t+1UL; } return g; }
static unsigned long r1(unsigned long x) { return (x << 1) | (x >> 31); }
static unsigned long r2(unsigned long x) { return (x >> 30) | (x << 2); }
static unsigned long r3(unsigned long x) { return (x << 3) | (x >> 29); }
static unsigned long r4(unsigned long x) { return (x >> 28) | (x << 4); }
static unsigned long r5(unsigned long x) { return (x << 5) | (x >> 27); }
static unsigned long r6(unsigned long x) { return (x >> 26) | (x << 6); }
static unsigned long r7(unsigned long x) { return (x << 7) | (x >> 25); }
static unsigned long r8(unsigned long x) { return (x >> 24) | (x << 8); }
static unsigned long r9(unsigned long x) { return (x << 9) | (x >> 23); }
static unsigned long r10(unsigned long x) { return (x >> 22) | (x << 10); }
static unsigned long r11(unsigned long x) { return (x << 11) | (x >> 21); }
static unsigned long r12(unsigned long x) { return (x >> 20) | (x << 12); }
static unsigned long r13(unsigned long x) { return (x << 13) | (x >> 19); }
static unsigned long r14(unsigned long x) { return (x >> 18) | (x << 14); }
static unsigned long r15(unsigned long x) { return (x << 15) | (x >> 17); }
static unsigned long r16(unsigned long x) { return (x >> 16) | (x << 16); }
static unsigned long r17(unsigned long x) { return (x << 17) | (x >> 15); }
static unsigned long r18(unsigned long x) { return (x >> 14) | (x << 18); }
static unsigned long r19(unsigned long x) { return (x << 19) | (x >> 13); }
static unsigned long r20(unsigned long x) { return (x >> 12) | (x << 20); }
static unsigned long r21(unsigned long x) { return (x << 21) | (x >> 11); }
static unsigned long r22(unsigned long x) { return (x >> 10) | (x << 22); }
static unsigned long r23(unsigned long x) { return (x << 23) | (x >> 9); }
static unsigned long r24(unsigned long x) { return (x >> 8) | (x << 24); }
static unsigned long r25(unsigned long x) { return (x << 25) | (x >> 7); }
static unsigned long r26(unsigned long x) { return (x >> 6) | (x << 26); }
static unsigned long r27(unsigned long x) { return (x << 27) | (x >> 5); }
static unsigned long r28(unsigned long x) { return (x >> 4) | (x << 28); }
static unsigned long r29(unsigned long x) { return (x << 29) | (x >> 3); }
static unsigned long r30(unsigned long x) { return (x >> 2) | (x << 30); }
static unsigned long r31(unsigned long x) { return (x << 31) | (x >> 1); }
typedef unsigned long (*rotator)(unsigned long);
static rotator rotate[31] = {r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18,r19,r20,r21,r22,r23,r24,r25,r26,r27,r28,r29,r30,r31};
static unsigned long values[6] = {0x00000000UL,0x00000001UL,0x80000000UL,0xffffffffUL,0x01234567UL,0xdeadbeefUL};
static unsigned long golden[31][6] = {
 {0x00000000UL,0x00000002UL,0x00000001UL,0xffffffffUL,0x02468aceUL,0xbd5b7ddfUL},
 {0x00000000UL,0x00000004UL,0x00000002UL,0xffffffffUL,0x048d159cUL,0x7ab6fbbfUL},
 {0x00000000UL,0x00000008UL,0x00000004UL,0xffffffffUL,0x091a2b38UL,0xf56df77eUL},
 {0x00000000UL,0x00000010UL,0x00000008UL,0xffffffffUL,0x12345670UL,0xeadbeefdUL},
 {0x00000000UL,0x00000020UL,0x00000010UL,0xffffffffUL,0x2468ace0UL,0xd5b7ddfbUL},
 {0x00000000UL,0x00000040UL,0x00000020UL,0xffffffffUL,0x48d159c0UL,0xab6fbbf7UL},
 {0x00000000UL,0x00000080UL,0x00000040UL,0xffffffffUL,0x91a2b380UL,0x56df77efUL},
 {0x00000000UL,0x00000100UL,0x00000080UL,0xffffffffUL,0x23456701UL,0xadbeefdeUL},
 {0x00000000UL,0x00000200UL,0x00000100UL,0xffffffffUL,0x468ace02UL,0x5b7ddfbdUL},
 {0x00000000UL,0x00000400UL,0x00000200UL,0xffffffffUL,0x8d159c04UL,0xb6fbbf7aUL},
 {0x00000000UL,0x00000800UL,0x00000400UL,0xffffffffUL,0x1a2b3809UL,0x6df77ef5UL},
 {0x00000000UL,0x00001000UL,0x00000800UL,0xffffffffUL,0x34567012UL,0xdbeefdeaUL},
 {0x00000000UL,0x00002000UL,0x00001000UL,0xffffffffUL,0x68ace024UL,0xb7ddfbd5UL},
 {0x00000000UL,0x00004000UL,0x00002000UL,0xffffffffUL,0xd159c048UL,0x6fbbf7abUL},
 {0x00000000UL,0x00008000UL,0x00004000UL,0xffffffffUL,0xa2b38091UL,0xdf77ef56UL},
 {0x00000000UL,0x00010000UL,0x00008000UL,0xffffffffUL,0x45670123UL,0xbeefdeadUL},
 {0x00000000UL,0x00020000UL,0x00010000UL,0xffffffffUL,0x8ace0246UL,0x7ddfbd5bUL},
 {0x00000000UL,0x00040000UL,0x00020000UL,0xffffffffUL,0x159c048dUL,0xfbbf7ab6UL},
 {0x00000000UL,0x00080000UL,0x00040000UL,0xffffffffUL,0x2b38091aUL,0xf77ef56dUL},
 {0x00000000UL,0x00100000UL,0x00080000UL,0xffffffffUL,0x56701234UL,0xeefdeadbUL},
 {0x00000000UL,0x00200000UL,0x00100000UL,0xffffffffUL,0xace02468UL,0xddfbd5b7UL},
 {0x00000000UL,0x00400000UL,0x00200000UL,0xffffffffUL,0x59c048d1UL,0xbbf7ab6fUL},
 {0x00000000UL,0x00800000UL,0x00400000UL,0xffffffffUL,0xb38091a2UL,0x77ef56dfUL},
 {0x00000000UL,0x01000000UL,0x00800000UL,0xffffffffUL,0x67012345UL,0xefdeadbeUL},
 {0x00000000UL,0x02000000UL,0x01000000UL,0xffffffffUL,0xce02468aUL,0xdfbd5b7dUL},
 {0x00000000UL,0x04000000UL,0x02000000UL,0xffffffffUL,0x9c048d15UL,0xbf7ab6fbUL},
 {0x00000000UL,0x08000000UL,0x04000000UL,0xffffffffUL,0x38091a2bUL,0x7ef56df7UL},
 {0x00000000UL,0x10000000UL,0x08000000UL,0xffffffffUL,0x70123456UL,0xfdeadbeeUL},
 {0x00000000UL,0x20000000UL,0x10000000UL,0xffffffffUL,0xe02468acUL,0xfbd5b7ddUL},
 {0x00000000UL,0x40000000UL,0x20000000UL,0xffffffffUL,0xc048d159UL,0xf7ab6fbbUL},
 {0x00000000UL,0x80000000UL,0x40000000UL,0xffffffffUL,0x8091a2b3UL,0xef56df77UL},
};
static unsigned long mixed(unsigned long x, unsigned long y) { return (x << 5) | (y >> 27); }
static unsigned long arithmetic(long x) { return ((unsigned long)x << 5) | (x >> 27); }
static unsigned long long_chain(unsigned long x) { return x+x+x+x-x+x; }
static unsigned long edges[7] = {0UL,255UL,65535UL,16777215UL,0xffffffffUL,0x80000000UL,0x7fffffffUL};
static unsigned long next[7] = {1UL,256UL,65536UL,16777216UL,0UL,0x80000001UL,0x80000000UL};
static unsigned long prev[7] = {0xffffffffUL,254UL,65534UL,16777214UL,0xfffffffeUL,0x7fffffffUL,0x7ffffffeUL};
static unsigned long changed_rotate(void) { unsigned long left=g<<5; g=0xfedcba98UL; return left | (g>>27); }
static unsigned long called_rotate(void) { unsigned long left=g<<5; bump(); return left | (g>>27); }
int main(void) { unsigned int n,j; unsigned long old,now;
 for(n=0;n<31;++n) for(j=0;j<6;++j) if(rotate[n](values[j]) != golden[n][j]) { printf("FAIL rotate %u %u\n",n+1,j); return 1; }
 for(j=0;j<7;++j) {
  g=edges[j]; update(1); if(g!=next[j]) { puts("FAIL increment"); return 1; }
  g=edges[j]; decrease(1); if(g!=prev[j]) { puts("FAIL decrement"); return 1; }
  g=edges[j]; old=g++; now=++g; if(old!=edges[j] || now!=next[j]+1UL) { puts("FAIL live result"); return 1; }
  g=edges[j]; if(alias(&g,1)!=next[j]) { puts("FAIL alias"); return 1; }
  g=edges[j]; if(call(1)!=next[j]) { puts("FAIL call"); return 1; }
  vg=edges[j]; ++vg; if(vg!=next[j]) { puts("FAIL volatile"); return 1; }
 }
 if(mixed(0x01234567UL,0xdeadbeefUL)!=0x2468acfbUL || arithmetic(-1L)!=0xffffffffUL || long_chain(0x12345678UL)!=0x48d159e0UL) { puts("FAIL nonrotate/chain"); return 1; }
 g=0x01234567UL; if(changed_rotate()!=0x2468acffUL) { puts("FAIL changed rotate"); return 1; }
 g=0x01234567UL; if(called_rotate()!=0x2468ace0UL) { puts("FAIL called rotate"); return 1; }
 puts("twidegen passed"); return 0;
}
