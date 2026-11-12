/* tasmdrop.c - #asm blocks next to static functions whose bodies are dropped
 *
 * A static inline function's body is buffered and dropped when every call to
 * it is inlined. A #asm block before such a function, between two of them, or
 * after the last one must still be emitted exactly once: each defines a public
 * label that C calls, so a lost block fails to assemble.
 */

#include <stdio.h>

extern int seven(void);
extern int eight(void);
extern int nine(void);

#asm
	public _seven
_seven:
	ld hl,7
	ret
#endasm

static inline int twice(int x)
{
    return x + x;
}

#asm
	public _eight
_eight:
	ld hl,8
	ret
#endasm

static inline int thrice(int x)
{
    return x + x + x;
}

#asm
	public _nine
_nine:
	ld hl,9
	ret
#endasm

int main(void)
{
    int ok;
    ok = 1;

    if (seven() != 7)           { printf("FAIL: seven\n");  ok = 0; }
    if (eight() != 8)           { printf("FAIL: eight\n");  ok = 0; }
    if (nine() != 9)            { printf("FAIL: nine\n");   ok = 0; }
    if (twice(seven()) != 14)   { printf("FAIL: twice\n");  ok = 0; }
    if (thrice(eight()) != 24)  { printf("FAIL: thrice\n"); ok = 0; }

    if (ok)
        printf("tasmdrop passed\n");
    return ok ? 0 : 1;
}
