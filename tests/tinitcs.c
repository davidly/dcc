/*
 * tinitcs.c - casting a string literal in a global pointer initializer.
 * dcc's global-initializer constant-expression path treated any leading
 * '(' as the start of a numeric constant expression (for forms like
 * `static long x = (MAXDIST * 2);`), so `(char *) "Plain"` fed the string
 * token into the numeric folder, which doesn't understand string literals
 * and silently desynced the lexer - DCC-E1102 "expected ';' near 'Plain'"
 * immediately followed by DCC-E1101. A bare `char *c1 = "Booting";` (no
 * cast) always worked; only a pointer cast directly ahead of a string
 * literal tripped it.
 */
#include <stdio.h>

static int fails;

static void chks(const char *name, const char *got, const char *exp)
{
    if (got == NULL || exp == NULL || got[0] != exp[0]) {
        printf("FAIL %s got '%s' expected '%s'\n", name, got, exp);
        fails++;
    }
}

char *c1 = "Booting";
char *c3 = (char *) "Plain";
const char *c4 = (const char *) "Const";

int main(void)
{
    fails = 0;

    chks("uncast", c1, "Booting");
    chks("cast", c3, "Plain");
    chks("const_cast", c4, "Const");

    if (fails) {
        printf("tinitcs failed: %d\n", fails);
        return 1;
    }
    printf("tinitcs completed with great success\n");
    return 0;
}
