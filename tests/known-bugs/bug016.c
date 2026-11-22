/* BUG-016: residual initializer signedness: positive long constants and
 * signed word right shifts. Integer seeds 70940, 91438 and 108843. */
#include <stdint.h>
#include <stdio.h>
static int32_t a(int32_t x) { int32_t d=(uint32_t)4; return x/d; }
static int32_t b(int32_t x) { int32_t d=(int32_t)4; return x/d; }
static int32_t c(int32_t x) { int32_t d=4; d=(uint32_t)4; return x/d; }
static int32_t d(void) { int32_t x=(uint32_t)88; return x/((int32_t)-80); }
static int32_t e(void) { int32_t x=(int32_t)88; return x/((int32_t)-80); }
static int32_t f(void) { int32_t x=(uint32_t)88; return x%((int32_t)-80); }
static int16_t s16a(uint16_t x) { int16_t l=x; return (l>>2)>>13; }
static int16_t s16b(uint16_t x) { int16_t l=x; return l>>2; }
static int16_t s16c(uint16_t x) { int16_t l=0; l=x; return (l>>2)>>13; }
static int16_t s16d(int16_t x) { int16_t l=x; return (l>>2)>>13; }
int main(void) {
 printf("a %ld\n",(long)a(-128)); printf("b %ld\n",(long)b(-128)); printf("c %ld\n",(long)c(-128));
 printf("d %ld\n",(long)d()); printf("e %ld\n",(long)e()); printf("f %ld\n",(long)f());
 printf("word chain %d\n",s16a((uint16_t)-67)); printf("word single %d\n",s16b((uint16_t)-67));
 printf("word separate %d\n",s16c((uint16_t)-67)); printf("word signed %d\n",s16d(-67)); return 0;
}
