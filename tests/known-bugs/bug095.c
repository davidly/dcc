/* BUG-095: comparison of two float member-array updates loses the left
 * value. Float seed 94893; final four lines are passing controls. */
#include <stdint.h>
#include <stdio.h>
struct S { float v[2]; }; static struct S s={{4.0f,1.5f}};
#define a s.v[0]
#define b s.v[1]
static void reset(void) { a=4.0f; b=1.5f; }
static uint32_t bits(float f) { union { float f; uint32_t u; } v; v.f=f; return v.u; }
static void show(uint16_t r) { printf("%u %08lx %08lx\n",r,(unsigned long)bits(a),(unsigned long)bits(b)); }
int main(void) {
 uint16_t r; float x,y;
 reset(); r=(++a)>(++b); show(r);
 reset(); r=(a++)>(b++); show(r);
 reset(); r=(--a)>(--b); show(r);
 reset(); r=(a--)>(b--); show(r);
 reset(); r=(++a)<(++b); show(r);
 reset(); r=(++a)==(++b); show(r);
 reset(); r=(++a)>b; show(r);
 reset(); r=a>(++b); show(r);
 reset(); x=++a; y=++b; r=x>y; show(r);
 reset(); r=(a=5.0f)>(b=2.5f); show(r);
 return 0;
}
