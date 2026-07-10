#include <stdio.h>

typedef long (*LongOp)(long, long);
typedef int (*MixedOp)(long, int);
typedef unsigned long (*HashFn)(const char *);

static long add(long a, long b) { return a + b; }
static long subtract(long a, long b) { return a - b; }
static int mixed(long a, int b) { return (int)(a + b); }

static unsigned long hash33(const char *text)
{
    unsigned long hash = 0;
    while (*text != '\0')
        hash = hash * 33UL + (unsigned char)*text++;
    return hash;
}

static const LongOp operations[2] = { add, subtract };

int main(void)
{
    LongOp table_call = operations[0];
    long (*direct_decl)(long, long) = subtract;
    MixedOp mixed_call = mixed;
    HashFn hash_call = hash33;
    long a = table_call(70000L, 5);
    long b = (*table_call)(-80000L, 7);
    long c = direct_decl(10, 3);
    int d = mixed_call(32000L, 9);
    unsigned long hash = hash_call("abc");

    printf("tlngfptr a=%ld b=%ld c=%ld d=%d hash=%lu\n",
           a, b, c, d, hash);
    return 0;
}
