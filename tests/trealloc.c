#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void fail(const char *msg)
{
    printf("FAIL %s\n", msg);
    fails++;
}

int main(void)
{
    unsigned char *p;
    unsigned char *q;
    unsigned int i;

    fails = 0;

    p = (unsigned char *)malloc(32U);
    if (p == 0) {
        printf("FAIL initial malloc returned null\n");
        return 1;
    }

    for (i = 0; i < 32U; i++)
        p[i] = (unsigned char)(0x80U + i);

    q = (unsigned char *)realloc(p, 0xfffeU);
    if (q != 0) {
        fail("realloc accepted impossible wrapped top growth");
        return 1;
    }

    for (i = 0; i < 32U; i++)
        if (p[i] != (unsigned char)(0x80U + i)) {
            fail("failed realloc clobbered original block");
            break;
        }

    q = (unsigned char *)malloc(16U);
    if (q == 0)
        fail("heap unusable after failed wrapped realloc");
    else {
        for (i = 0; i < 16U; i++)
            q[i] = (unsigned char)i;
        for (i = 0; i < 16U; i++)
            if (q[i] != (unsigned char)i) {
                fail("post-failure malloc returned corrupted block");
                break;
            }
        for (i = 0; i < 32U; i++)
            if (p[i] != (unsigned char)(0x80U + i)) {
                fail("post-failure malloc overlapped original block");
                break;
            }
        free(q);
    }

    free(p);

    if (fails != 0) {
        printf("trealloc FAILED: %d\n", fails);
        return 1;
    }

    printf("trealloc: all tests passed\n");
    return 0;
}
