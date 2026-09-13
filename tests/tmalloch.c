#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef W28_RENAMED_RUNTIME
extern void *w28_allocate(unsigned int size);
extern void w28_release(void *pointer);
extern int w28_print(const char *format, ...);
#define malloc w28_allocate
#define free w28_release
#define printf w28_print
#elif defined(W28_WIDE_SIZE_RUNTIME)
extern void *w28la(unsigned long size);
#define malloc w28la
#elif defined(W28_FAST_RUNTIME)
extern void *__fastcall w28fa(unsigned int size);
extern void __fastcall w28fr(void *pointer);
#define malloc w28fa
#define free w28fr
#endif

#ifdef W28_VOLATILE_BYTES
typedef volatile unsigned char *W28BytePointer;
#else
typedef unsigned char *W28BytePointer;
#endif

#ifdef W28_VOLATILE_LOCALS
#define W28_LOCAL_QUALIFIER volatile
#else
#define W28_LOCAL_QUALIFIER
#endif

int main(void)
{
    W28BytePointer W28_LOCAL_QUALIFIER p;
    W28BytePointer W28_LOCAL_QUALIFIER q;
    void *r;
    unsigned int i;
    unsigned long sum;

    p = (W28BytePointer)malloc(32768U);
    if (!p) {
        printf("tmallochi2: malloc32768 failed\n");
        return 1;
    }

    p[0] = 0x12;
    p[32767U] = 0x34;
    sum = (unsigned long)p[0] + (unsigned long)p[32767U];
    if (sum != 0x46UL) {
        printf("FAIL large block sum got %lu expected %lu\n", sum, 0x46UL);
        return 1;
    }

    free(p);

    q = (W28BytePointer)malloc(32U);
    if (!q) {
        printf("FAIL malloc after large free returned null\n");
        return 1;
    }

    for (i = 0; i < 32U; i++)
        q[i] = (unsigned char)i;

    if (q[0] != 0U || q[31] != 31U) {
        printf("FAIL small block after large free\n");
        return 1;
    }

    free(q);

    p = malloc(32768U);
    if (p != 0) {
        free(p);
    }

    q = malloc(1U);
    if (q == 0) {
        printf("FAIL small malloc failed\n");
        return 1;
    }

    /* This must fail on any valid CP/M .COM heap after one live allocation:
       it cannot fit without 16-bit address wrap or colliding with the stack. */
    r = malloc(65000U);
    if (r != 0) {
        printf("FAIL malloc wrap accepted impossible heap growth\n");
        return 1;
    }

    free(q);

    printf("tmalloch: all tests passed\n");
    return 0;
}
