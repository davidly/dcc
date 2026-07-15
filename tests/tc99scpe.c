/*
 * tc99scpe.c - C99 declarations within source (mid-block variable declarations).
 *
 * Tests that variables can be declared anywhere within a block, not just
 * at the top. This includes declarations after statements, within if/while/
 * for bodies, and with proper scope semantics.
 */
#include <stdio.h>

static int fails;

static void chk(long got, long want, const char *name)
{
    if (got != want) {
        printf("FAIL %s got=%ld want=%ld\n", name, got, want);
        fails++;
    }
}

/* Test: simple mid-block declaration */
static int mid_block_simple(void)
{
    int x = 10;
    x += 5;
    int y = 20;  /* declared after statement */
    return x + y;  /* 10 + 5 + 20 = 35 */
}

/* Test: multiple mid-block declarations */
static int mid_block_multiple(void)
{
    int a = 1;
    a += 2;
    int b = 3;
    b += 4;
    int c = 5;
    return a + b + c;  /* 3 + 7 + 5 = 15 */
}

/* Test: for-loop init with loop-scoped variable */
static int for_init_scope(void)
{
    int sum = 0;
    
    for (int i = 0; i < 3; i++) {
        sum += i;
    }
    /* i is out of scope here */
    
    /* Can reuse name in separate loop. */
    for (int i = 10; i < 13; i++) {
        sum += i;
    }
    
    return sum;  /* 0+1+2 + 10+11+12 = 36 */
}

/* Test: declaration in if body */
static int if_body_decl(int flag)
{
    int result = 100;
    
    if (flag) {
        int x = 50;  /* declared in if body */
        result += x;
    }
    /* x is out of scope here */
    
    return result;
}

/* Test: declaration in while body */
static int while_body_decl(void)
{
    int sum = 0;
    int count = 3;
    
    while (count > 0) {
        int step = 10;  /* declared in while body */
        sum += step;
        count--;
    }
    /* step is out of scope here */
    
    return sum;  /* 10 + 10 + 10 = 30 */
}

/* Test: nested block scope with shadowing */
static int nested_shadow(void)
{
    int x = 5;
    int result = 0;
    
    {
        int y = 10;
        result += x + y;  /* 5 + 10 = 15 */
        
        {
            int x = 100;  /* shadows outer x */
            int z = 1;    /* declared in inner block */
            result += x + z;  /* 100 + 1 = 101 */
        }
        /* inner x and z out of scope, outer x restored */
        result += x;  /* 5 */
    }
    
    return result;  /* 15 + 101 + 5 = 121 */
}

/* Test: declaration in switch body */
static int switch_body_decl(int choice)
{
    int result = 0;
    
    switch (choice) {
    case 1: {
        int val = 100;
        result = val;
        break;
    }
    case 2: {
        int val = 200;
        result = val;
        break;
    }
    default:
        result = 0;
    }
    
    return result;
}

/* Test: conditional expression with side effects */
static int conditional_decl(void)
{
    int x = 5;
    int y;
    
    if (x > 0)
        y = 100;
    else
        y = 200;
    
    int z = x + y;
    return z;  /* 5 + 100 = 105 */
}

/* Test: for-loop init with multiple declarators */
static int for_multi_declarators(void)
{
    int sum = 0;
    
    for (int i = 0, j = 10; i < 3; i++, j--) {
        sum += i + j;  /* (0+10) + (1+9) + (2+8) = 30 */
    }
    
    return sum;
}

/* Test: pointer declaration mid-block */
static int pointer_mid_block(void)
{
    int arr[] = { 10, 20, 30 };
    int sum = 0;
    
    for (int i = 0; i < 3; i++) {
        int *p = &arr[i];  /* declare pointer in loop */
        sum += *p;
    }
    
    return sum;  /* 10 + 20 + 30 = 60 */
}

/* Test: pointer for-init with a sizeof-based compound update */
static int pointer_for_init_sizeof(void)
{
    static const char prefix[] = "abc";
    static const char fallback[] = "none";
    const char text[] = "abcx";
    int result = 0;

    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor == prefix[0])
            cursor += sizeof(prefix) - 1;
        result += *cursor;
    }

    return result + (int)sizeof(fallback);  /* 'x' + 5 = 125 */
}

int main(void)
{
    fails = 0;
    
    /* Test mid-block declarations. */
    chk(mid_block_simple(), 35, "mid_block_simple");
    chk(mid_block_multiple(), 15, "mid_block_multiple");
    
    /* Test for-loop init scope. */
    chk(for_init_scope(), 36, "for_init_scope");
    chk(for_multi_declarators(), 30, "for_multi_declarators");
    
    /* Test declarations in control flow bodies. */
    chk(if_body_decl(1), 150, "if_body_decl(1)");
    chk(if_body_decl(0), 100, "if_body_decl(0)");
    chk(while_body_decl(), 30, "while_body_decl");
    
    /* Test nested scope and shadowing. */
    chk(nested_shadow(), 121, "nested_shadow");
    
    /* Test switch body scope. */
    chk(switch_body_decl(1), 100, "switch_body_decl(1)");
    chk(switch_body_decl(2), 200, "switch_body_decl(2)");
    chk(switch_body_decl(3), 0, "switch_body_decl(3)");
    
    /* Test conditional with declaration. */
    chk(conditional_decl(), 105, "conditional_decl");
    
    /* Test pointer declaration in loop. */
    chk(pointer_mid_block(), 60, "pointer_mid_block");
    chk(pointer_for_init_sizeof(), 125, "pointer_for_init_sizeof");
    
    if (fails == 0)
        printf("tc99scpe passed with great success\n");
    
    return fails;
}
