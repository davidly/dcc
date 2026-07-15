#include <stdio.h>
#include <string.h>
#include <stdint.h>

static char g_buffer[16];
static uint8_t g_static_len;
uint8_t g_global_len;

/* Regression: a global/static uint8_t post-incremented inside an array
 * subscript (buffer[len++] = ...) used to be rejected with DCC-E1002. */
static void test_global_byte_index(void)
{
    memset(g_buffer, 0, sizeof(g_buffer));
    g_static_len = 0;
    g_global_len = 0;

    g_buffer[g_static_len++] = 'A';
    g_buffer[g_static_len++] = 'B';
    printf("static u8 idx: buf=%c%c len=%u\n",
           g_buffer[0], g_buffer[1], (unsigned)g_static_len); /* AB 2 */

    g_buffer[g_global_len++] = 'X';
    g_buffer[g_global_len++] = 'Y';
    g_buffer[g_global_len++] = 'Z';
    printf("global u8 idx: buf=%c%c%c len=%u\n",
           g_buffer[0], g_buffer[1], g_buffer[2],
           (unsigned)g_global_len);                            /* XYZ 3 */
}

static void test_char_simple(void)
{
    char ac[10];
    strcpy(ac, "test");
    char *p = ac;

    char c = (*p)++;
    printf("char post++: old=%c new=%c\n", c, *p);  /* old=t new=u */

    char d = (*p)--;
    printf("char post--: old=%c new=%c\n", d, *p);  /* old=u new=t */
}

static void test_char_ptr_math(void)
{
    char ac[10];
    strcpy(ac, "test");
    char *p = ac;

    /* pointer + constant */
    char e = (*(p+1))++;
    printf("char ptr+1 post++: old=%c new=%c\n", e, ac[1]);  /* old=e new=f */

    /* pointer + variable */
    int i = 2;
    char f = (*(p+i))++;
    printf("char ptr+i post++: old=%c new=%c\n", f, ac[2]);  /* old=s new=t */

    /* pointer + variable, post-decrement */
    int j = 3;
    char g = (*(p+j))--;
    printf("char ptr+j post--: old=%c new=%c\n", g, ac[3]);  /* old=t new=s */
}

static void test_int_simple(void)
{
    int ai[4];
    ai[0] = 10; ai[1] = 20; ai[2] = 30; ai[3] = 40;
    int *ip = ai;

    int x = (*ip)++;
    printf("int post++: old=%d new=%d\n", x, *ip);   /* old=10 new=11 */

    int y = (*ip)--;
    printf("int post--: old=%d new=%d\n", y, *ip);   /* old=11 new=10 */
}

static void test_int_ptr_math(void)
{
    int ai[4];
    ai[0] = 100; ai[1] = 200; ai[2] = 300; ai[3] = 400;
    int *ip = ai;

    /* pointer + constant */
    int a = (*(ip+1))++;
    printf("int ptr+1 post++: old=%d new=%d\n", a, ai[1]);  /* old=200 new=201 */

    /* pointer + variable */
    int k = 2;
    int b = (*(ip+k))--;
    printf("int ptr+k post--: old=%d new=%d\n", b, ai[2]);  /* old=300 new=299 */
}

extern int main()
{
    test_char_simple();
    test_char_ptr_math();
    test_int_simple();
    test_int_ptr_math();
    test_global_byte_index();
    return 0;
}
