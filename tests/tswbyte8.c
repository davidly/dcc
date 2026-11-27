/* Exhaustive dense byte-switch bounds/holes, high-byte and volatile controls. */
#include <stdio.h>

struct Packet { unsigned char op; int arg; };
static unsigned int calls;
static unsigned int reads;
static volatile unsigned char selector;

static int side(int x, int tag)
{
    ++calls;
    return x + 3 * tag;
}

#define ARM(k,t) case k: return side(p->arg, t)
static int sparse(struct Packet *p)
{
    switch (p->op) {
    ARM(1,3); ARM(2,7); ARM(4,11); ARM(7,13);
    ARM(9,17); ARM(12,19); ARM(14,23); ARM(15,29);
    default: return p->arg - 11;
    }
}
static int upper(struct Packet *p)
{
    switch (p->op) {
    ARM(248,1); ARM(249,2); ARM(250,3); ARM(251,4);
    ARM(252,5); ARM(253,6); ARM(254,7); ARM(255,8);
    default: return p->arg - 11;
    }
}
static int lower(struct Packet *p)
{
    switch (p->op) {
    ARM(0,1); ARM(1,2); ARM(2,3); ARM(3,4);
    ARM(4,5); ARM(5,6); ARM(6,7); ARM(7,8);
    default: return p->arg - 11;
    }
}
#undef ARM
#define NUM_ARMS \
    case 1: return side(x,1); case 2: return side(x,2); \
    case 3: return side(x,3); case 4: return side(x,4); \
    case 5: return side(x,5); case 6: return side(x,6); \
    case 7: return side(x,7); case 8: return side(x,8)
static int word_control(unsigned int op, int x)
{
    switch (op) { NUM_ARMS; default: return x - 11; }
}
static int signed_control(signed char op, int x)
{
    switch (op) { NUM_ARMS; default: return x - 11; }
}
static unsigned char read_selector(void)
{
    ++reads;
    return selector;
}
static int observable(int x)
{
    switch (read_selector()) { NUM_ARMS; default: return x - 11; }
}
#undef NUM_ARMS

static int expected(int x, int tag)
{
    return tag ? x + 3 * tag : x - 11;
}
int main(void)
{
    static const int tags[16] = {0,3,7,0,11,0,0,13,0,17,0,0,19,0,23,29};
    struct Packet p;
    int i, x, tag, result;
    unsigned int before;
    for (i = 0; i < 256; ++i) {
        p.op = (unsigned char)i;
        p.arg = x = 3 * i - 300;
        tag = i < 16 ? tags[i] : 0;
        before = calls;
        result = sparse(&p);
        if (result != expected(x,tag) || calls != before + (tag != 0)) goto fail;
        tag = i >= 248 ? i - 247 : 0;
        before = calls;
        result = upper(&p);
        if (result != expected(x,tag) || calls != before + (tag != 0)) goto fail;
        tag = i < 8 ? i + 1 : 0;
        before = calls;
        result = lower(&p);
        if (result != expected(x,tag) || calls != before + (tag != 0)) goto fail;
        tag = i >= 1 && i <= 8 ? i : 0;
        before = calls;
        result = signed_control((signed char)i,x);
        if (result != expected(x,tag) || calls != before + (tag != 0)) goto fail;
        before = calls;
        if (word_control((unsigned int)i + 256U,x) != x - 11 || calls != before) goto fail;
        selector = (unsigned char)i;
        before = reads;
        result = observable(x);
        if (result != expected(x,tag) || reads != before + 1U) goto fail;
    }
    puts("byte switches passed");
    return 0;
fail:
    printf("byte switch FAIL at %d\n",i);
    return 1;
}
