/*
 * tginaddr.c - grouped address constants in global initializers.
 *
 * Parenthesized or pointer-cast symbol arithmetic is still a relocatable
 * address constant.  dcc previously sent every initializer beginning with
 * '(' to its numeric folder, which rejected the symbol and desynchronized
 * parsing.  Found via z88dk's Issue_2523_global_init.c regression test.
 */
#include <stdio.h>

enum FileType {
    FILE_UNDEF,
    FILE_SYSTEM,
    FILE_SPECIAL,
    FILE_USER
};

struct Keyword {
    char *key;
    int type;
    char id;
};

static char buffer[16];
static char *p = (char *)(buffer + 1);
static char *q = (char *)buffer + 1;
static char *r = ((char *)buffer + 1);
static int folded = ((1) + (1023));
static struct Keyword words[] = {
    { "read", ((FILE_SYSTEM) * 1024 + (3)), 2 }
};

static int fails;

static void check(const char *name, int condition)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        ++fails;
    }
}

int main(void)
{
    fails = 0;
    check("cast grouped", p == buffer + 1);
    check("cast direct", q == buffer + 1);
    check("outer grouped", r == buffer + 1);
    check("numeric grouping", folded == 1024);
    check("aggregate numeric", words[0].type == 1027);
    check("aggregate string", words[0].key[0] == 'r');
    check("aggregate byte", words[0].id == 2);

    if (fails) {
        printf("tginaddr failed: %d\n", fails);
        return 1;
    }
    printf("tginaddr completed with great success\n");
    return 0;
}
