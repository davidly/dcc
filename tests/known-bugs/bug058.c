/* BUG-058: cross-signed word initializers in nested blocks retain the
 * source signedness when widened into a 32-bit array. Scalar stores,
 * function-scope locals, separate assignments and same-type controls pass. */
#include <stdint.h>
#include <stdio.h>
int32_t g, a[2];
static void block_init(uint16_t x) { { int16_t b=x; g=b; a[1]=b; } }
static void function_init(uint16_t x) { int16_t b=x; g=b; a[1]=b; }
static void block_assign(uint16_t x) { { int16_t b; b=x; g=b; a[1]=b; } }
static void block_cast(uint16_t x) { { int16_t b=(int16_t)x; g=b; a[1]=b; } }
static void block_literal(void) { { int16_t b=-32768; g=b; a[1]=b; } }
static void block_copy(uint16_t x) { { uint16_t b=x; int16_t c=b; g=c; a[1]=c; } }
static void block_signed(int16_t x) { { int16_t b=x; g=b; a[1]=b; } }
static void block_expr(uint16_t x) { { int16_t b=x+0; g=b; a[1]=b; } }
static void block_unsigned(int16_t x) { { uint16_t b=x; g=b; a[1]=b; } }
int main(void) {
 block_init(0x8000); printf("block init %ld %ld\n",(long)g,(long)a[1]);
 function_init(0x8000); printf("function init %ld %ld\n",(long)g,(long)a[1]);
 block_assign(0x8000); printf("block assign %ld %ld\n",(long)g,(long)a[1]);
 block_cast(0x8000); printf("block cast %ld %ld\n",(long)g,(long)a[1]);
 block_literal(); printf("block literal %ld %ld\n",(long)g,(long)a[1]);
 block_copy(0x8000); printf("block copy %ld %ld\n",(long)g,(long)a[1]);
 block_signed(-32768); printf("block signed %ld %ld\n",(long)g,(long)a[1]);
 block_expr(0x8000); printf("block expr %ld %ld\n",(long)g,(long)a[1]);
 block_unsigned(-32768); printf("block unsigned %ld %ld\n",(long)g,(long)a[1]);
 return 0;
}
