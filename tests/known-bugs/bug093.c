/* BUG-093: unsigned right shift of a signed bit-field; integer seed 86893. */
#include <stdint.h>
#include <stdio.h>
struct S { int16_t b:5; }; static struct S s={-12};
static int32_t a(void) { return ((uint16_t)s.b) >> 4; }
static int32_t b(void) { return ((uint16_t)s.b) / 16; }
static int32_t c(void) { uint16_t x=s.b; return x >> 4; }
static int32_t d(void) { return ((uint16_t)s.b) >> 0; }
static uint16_t e(void) { return ((uint16_t)s.b) >> 4; }
static int16_t plain=-12;
struct U { uint16_t b:5; }; static struct U u={20};
int main(void) { printf("a %ld\n",(long)a()); printf("b %ld\n",(long)b()); printf("c %ld\n",(long)c()); printf("d %ld\n",(long)d()); printf("e %u\n",e());
 printf("scalar %u\n",((uint16_t)plain)>>4); printf("unsigned field %u\n",((uint16_t)u.b)>>4);
 printf("wide cast %u\n",((uint16_t)(int32_t)s.b)>>4); return 0; }
