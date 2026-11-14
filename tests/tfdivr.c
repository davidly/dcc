/* Float division rounding: four directed cases plus the 20x20 operand
 * matrix, compared as IEEE single-precision bits. Volatile operands keep
 * host division dynamic; explicit wrappers cover both __fdf and __fdiv,
 * checking that each entry preserves IY. */
#include <stdint.h>
#include <stdio.h>

typedef union { float f; uint32_t u; } bits;

#ifdef _DCC_
extern uint32_t fast(uint32_t a, uint32_t b);
extern uint32_t stack(uint32_t a, uint32_t b);
extern uint16_t iybad;
#asm
     extrn __fdf,__fdiv
     public _fast,_stack,_iybad
_iybad: dw 0
_fast:
     push ix
     ld ix,0
     add ix,sp
     push iy
     ld iy,12345
     ld l,(ix+4)
     ld h,(ix+5)
     ld e,(ix+6)
     ld d,(ix+7)
     push de
     push hl
     ld l,(ix+8)
     ld h,(ix+9)
     ld e,(ix+10)
     ld d,(ix+11)
     call __fdf
     jr R1IY
_stack:
     push ix
     ld ix,0
     add ix,sp
     push iy
     ld iy,12345
     ld l,(ix+4)
     ld h,(ix+5)
     ld e,(ix+6)
     ld d,(ix+7)
     push de
     push hl
     ld l,(ix+8)
     ld h,(ix+9)
     ld e,(ix+10)
     ld d,(ix+11)
     push de
     push hl
     call __fdiv
R1IY:
     push hl
     push iy
     pop hl
     ld bc,12345
     or a
     sbc hl,bc
     jr z,R1GOOD
     ld (_iybad),hl
R1GOOD:
     pop hl
     ld sp,ix
     dec sp
     dec sp
     pop iy
     pop ix
     ret
#endasm
#else
static uint16_t iybad;
static uint32_t fast(uint32_t a, uint32_t b)
{
    union { float f; uint32_t u; } x, y, z;
    volatile float numerator, denominator;
    x.u = a;
    y.u = b;
    numerator = x.f;
    denominator = y.f;
    z.f = numerator / denominator;
    return z.u;
}
#define stack fast
#endif

static const float vals[] = {
    1.0f, 3.0f, 0.1f, 0.2f, 1.9f, 3.14159f, 2.71828f, 123456.0f, 0.000123f,
    -7.5f, 1e-5f, 65535.0f, 32767.0f, 1e10f, 1e-10f, 16777216.0f, 0.3f, 0.7f,
    123.456f, 9.99999f
};
#define N (sizeof(vals) / sizeof(vals[0]))

volatile float va, vb;

static void show(const char *tag)
{
    bits a, b;
    a.f = va;
    b.f = vb;
    printf("fast %s %08lx\n", tag, (unsigned long)fast(a.u, b.u));
    printf("stack %s %08lx\n", tag, (unsigned long)stack(a.u, b.u));
}

int main(void)
{
    unsigned i, j;
    char tag[16];

    /* well-known cases: want 3eaaaaab 3ec4ec4e 3f0a60dd 40555555 */
    va = 1.0f; vb = 3.0f; show("1/3");
    va = 3.0f; vb = 7.8f; show("3/7.8");
    va = 1.0f; vb = 1.85f; show("1/1.85");
    va = 10.0f; vb = 3.0f; show("10/3");

    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            va = vals[i];
            vb = vals[j];
            sprintf(tag, "%u/%u", i, j);
            show(tag);
        }
    }
    printf("IY %u\n", (unsigned)iybad);
    return iybad ? 1 : 0;
}
