/* Exhaust byte products, then wraparound word/byte products in both
 * operand orders. Expected byte products use addition, not multiplication.
 * The assembler wrapper checks __mulu's AF/BC preservation contract. */
#include <stdio.h>
#include <stdint.h>

unsigned int bad;
unsigned int product(unsigned int a, unsigned int b);
unsigned int words[] = {0,1,15,16,255,256,257,511,32767,32768,65535};
struct Pair { unsigned int a,b,result; };
struct Pair pairs[] = {
    {0u,0u,0u},
    {0u,1u,0u},
    {0u,15u,0u},
    {0u,16u,0u},
    {0u,255u,0u},
    {0u,256u,0u},
    {0u,257u,0u},
    {0u,511u,0u},
    {0u,32767u,0u},
    {0u,32768u,0u},
    {0u,65535u,0u},
    {1u,0u,0u},
    {1u,1u,1u},
    {1u,15u,15u},
    {1u,16u,16u},
    {1u,255u,255u},
    {1u,256u,256u},
    {1u,257u,257u},
    {1u,511u,511u},
    {1u,32767u,32767u},
    {1u,32768u,32768u},
    {1u,65535u,65535u},
    {15u,0u,0u},
    {15u,1u,15u},
    {15u,15u,225u},
    {15u,16u,240u},
    {15u,255u,3825u},
    {15u,256u,3840u},
    {15u,257u,3855u},
    {15u,511u,7665u},
    {15u,32767u,32753u},
    {15u,32768u,32768u},
    {15u,65535u,65521u},
    {16u,0u,0u},
    {16u,1u,16u},
    {16u,15u,240u},
    {16u,16u,256u},
    {16u,255u,4080u},
    {16u,256u,4096u},
    {16u,257u,4112u},
    {16u,511u,8176u},
    {16u,32767u,65520u},
    {16u,32768u,0u},
    {16u,65535u,65520u},
    {255u,0u,0u},
    {255u,1u,255u},
    {255u,15u,3825u},
    {255u,16u,4080u},
    {255u,255u,65025u},
    {255u,256u,65280u},
    {255u,257u,65535u},
    {255u,511u,64769u},
    {255u,32767u,32513u},
    {255u,32768u,32768u},
    {255u,65535u,65281u},
    {256u,0u,0u},
    {256u,1u,256u},
    {256u,15u,3840u},
    {256u,16u,4096u},
    {256u,255u,65280u},
    {256u,256u,0u},
    {256u,257u,256u},
    {256u,511u,65280u},
    {256u,32767u,65280u},
    {256u,32768u,0u},
    {256u,65535u,65280u},
    {257u,0u,0u},
    {257u,1u,257u},
    {257u,15u,3855u},
    {257u,16u,4112u},
    {257u,255u,65535u},
    {257u,256u,256u},
    {257u,257u,513u},
    {257u,511u,255u},
    {257u,32767u,32511u},
    {257u,32768u,32768u},
    {257u,65535u,65279u},
    {511u,0u,0u},
    {511u,1u,511u},
    {511u,15u,7665u},
    {511u,16u,8176u},
    {511u,255u,64769u},
    {511u,256u,65280u},
    {511u,257u,255u},
    {511u,511u,64513u},
    {511u,32767u,32257u},
    {511u,32768u,32768u},
    {511u,65535u,65025u},
    {32767u,0u,0u},
    {32767u,1u,32767u},
    {32767u,15u,32753u},
    {32767u,16u,65520u},
    {32767u,255u,32513u},
    {32767u,256u,65280u},
    {32767u,257u,32511u},
    {32767u,511u,32257u},
    {32767u,32767u,1u},
    {32767u,32768u,32768u},
    {32767u,65535u,32769u},
    {32768u,0u,0u},
    {32768u,1u,32768u},
    {32768u,15u,32768u},
    {32768u,16u,0u},
    {32768u,255u,32768u},
    {32768u,256u,0u},
    {32768u,257u,32768u},
    {32768u,511u,32768u},
    {32768u,32767u,32768u},
    {32768u,32768u,0u},
    {32768u,65535u,32768u},
    {65535u,0u,0u},
    {65535u,1u,65535u},
    {65535u,15u,65521u},
    {65535u,16u,65520u},
    {65535u,255u,65281u},
    {65535u,256u,65280u},
    {65535u,257u,65279u},
    {65535u,511u,65025u},
    {65535u,32767u,32769u},
    {65535u,32768u,32768u},
    {65535u,65535u,1u},
};
#asm
        extrn __mulu
_product:
        push ix
        ld ix,0
        add ix,sp
        ld l,(ix+4)
        ld h,(ix+5)
        ld e,(ix+6)
        ld d,(ix+7)
        ld bc,42330
        ld a,90
        scf
        call __mulu
        jr nc,MPFAIL
        cp 90
        jr nz,MPFAIL
        ld a,b
        cp 165
        jr nz,MPFAIL
        ld a,c
        cp 90
        jr z,MPDONE
MPFAIL:
        ld a,1
        ld (_bad),a
MPDONE:
        pop ix
        ret
#endasm

int main(void)
{
    unsigned int a,b,i,expected;
    unsigned long checks,errors;
    checks=0; errors=0;
    for(a=0;a<256;++a) {
        expected=0;
        for(b=0;b<256;++b) {
            if(product(a,b)!=expected) errors++;
            expected+=a;
            checks++;
        }
    }
    for(i=0;i<11;++i) {
        a=words[i]; expected=0;
        for(b=0;b<256;++b) {
            if(product(a,b)!=expected) errors++;
            if(product(b,a)!=expected) errors++;
            expected+=a;
            checks+=2;
        }
    }
    for(i=0;i<121;++i) {
        if(product(pairs[i].a,pairs[i].b)!=pairs[i].result) errors++;
        checks++;
    }
    printf("products=%lu preserved=%d errors=%lu\n",checks,bad==0,errors);
    return bad!=0 || errors!=0;
}
