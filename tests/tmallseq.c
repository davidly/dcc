#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *blocks[1000];

static void require(int ok, const char *message)
{
    if (!ok) {
        printf("FAIL %s\n", message);
        exit(1);
    }
}

static void sequential(void)
{
    unsigned int i;
    unsigned char *p;

    for (i = 0; i < 1000U; ++i) {
        blocks[i] = (unsigned char *)malloc(16U);
        require(blocks[i] != 0, "sequential allocation");
        memset(blocks[i], (unsigned char)i, 16U);
    }
    for (i = 0; i < 1000U; ++i) {
        require(blocks[i][0] == (unsigned char)i &&
                blocks[i][15] == (unsigned char)i, "sequential contents");
    }
    /* Reuse an early hole, then trim back through the entire heap. */
    free(blocks[0]);
    require(malloc(16U) == blocks[0], "early free after long prefix");
    /* Consuming that hole leaves a long used prefix to retire once. */
    for (i = 0; i < 100U; ++i) {
        p = (unsigned char *)malloc(16U);
        require((unsigned)p == (unsigned)blocks[999] + 20U,
                "extension after early-hole reuse");
        free(p);
    }
    for (i = 1000U; i != 0; --i)
        free(blocks[i - 1U]);
}

static void first_fit(void)
{
    unsigned char *a, *b, *c, *guard, *p, *q, *r;

    a = (unsigned char *)malloc(4U);
    b = (unsigned char *)malloc(16U);
    c = (unsigned char *)malloc(64U);
    guard = (unsigned char *)malloc(16U);
    require(a && b && c && guard, "first-fit setup");
    free(a);
    free(c);
    p = (unsigned char *)malloc(32U);
    require(p == c, "skip small hole and split later hole");
    q = (unsigned char *)malloc(4U);
    require(q == a, "retain earlier small hole");
    r = (unsigned char *)malloc(16U);
    require((unsigned)r == (unsigned)c + 36U,
            "find later remainder after whole-block reuse");
    free(r);
    free(q);
    free(p);
    free(b);
    p = (unsigned char *)malloc(92U);
    require(p == a, "coalesced hint anchor");
    free(p);
    free(guard);
}

static void resized(void)
{
    unsigned char *a, *b, *guard, *p, *q;

    a = (unsigned char *)malloc(48U);
    b = (unsigned char *)malloc(16U);
    guard = (unsigned char *)malloc(16U);
    require(a && b && guard, "resize setup");
    free(b);
    p = (unsigned char *)realloc(a, 68U);
    require(p == a, "absorb hinted free block");
    /* Destroy the absorbed header: a stale hint must never read payload. */
    memset(p, 255, 68U);
    q = (unsigned char *)malloc(8U);
    require(q && q != b, "allocation after absorbed header overwritten");
    free(q);
    p = (unsigned char *)realloc(p, 16U);
    require(p == a, "shrink creates earlier free tail");
    q = (unsigned char *)malloc(48U);
    require((unsigned)q == (unsigned)a + 20U, "reuse shrink tail");
    free(q);
    free(guard);
    p = (unsigned char *)realloc(p, 128U);
    require(p == a, "grow heap-top block");
    memset(p, 255, 128U);
    q = (unsigned char *)malloc(16U);
    require((unsigned)q == (unsigned)p + 132U,
            "allocation after heap-top growth");
    free(q);
    free(p);
}

int main(void)
{
    sequential();
    first_fit();
    resized();
    puts("tmallseq: all tests passed");
    return 0;
}
