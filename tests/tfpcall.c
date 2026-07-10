#include <stdio.h>
#include <string.h>

extern int bdos(int fn, int dearg);

/* Regression test for a coverage gap: DCCRTL.MAC's fastcall entries
 * (__slf, __chf, __cmpf, __bdosf, __msf, __mcf, __mhf, __scf, __rcf,
 * __ssf) are only reached by dcc's compiler-side special case in
 * dcc_ast_gen_expr.c, which recognizes a direct call to the function by
 * name. Calling the same functions through a function pointer instead
 * forces the general (stack-marshaling) entry points (__slen, __schr,
 * __mcmp, _bdos, __mset, __mcpy, __mchr, __scpy, __srch, __sstr) and
 * dccrtlstrip's extraction of them, neither of which the fastcall special
 * case touches. Nothing in the rest of the suite exercised that path
 * before this test - every other call site calls these functions
 * directly by name. */

size_t (*fp_strlen)(const char *) = strlen;
char *(*fp_strchr)(const char *, int) = strchr;
int (*fp_memcmp)(const void *, const void *, size_t) = memcmp;
void *(*fp_memset)(void *, int, size_t) = memset;
int (*fp_bdos)(int, int) = bdos;
void *(*fp_memcpy)(void *, const void *, size_t) = memcpy;
void *(*fp_memchr)(const void *, int, size_t) = memchr;
char *(*fp_strcpy)(char *, const char *) = strcpy;
char *(*fp_strrchr)(const char *, int) = strrchr;
char *(*fp_strstr)(const char *, const char *) = strstr;

char msetbuf[8];
char mcpybuf[16];
char scpybuf[16];

int main(void)
{
    const char *s = "hello world";

    printf("%d\n", (int)fp_strlen(s));
    printf("%s\n", fp_strchr(s, 'w'));
    printf("%d\n", fp_memcmp("abc", "abd", 3));
    printf("%d\n", fp_memcmp("abc", "abc", 3));

    fp_memset(msetbuf, 'Z', sizeof(msetbuf));
    printf("%c%c%c%c%c%c%c%c\n",
           msetbuf[0], msetbuf[1], msetbuf[2], msetbuf[3],
           msetbuf[4], msetbuf[5], msetbuf[6], msetbuf[7]);

    /* BDOS 2: console output; echoes the char and returns it */
    fp_bdos(2, 'Q');
    printf("\n");

    fp_memcpy(mcpybuf, s, 5);
    mcpybuf[5] = 0;
    printf("%s\n", mcpybuf);

    {
        void *found = fp_memchr(s, 'w', strlen(s));
        printf("%d\n", found ? (int)((const char *)found - s) : -1);
    }

    fp_strcpy(scpybuf, s);
    printf("%s\n", scpybuf);

    printf("%s\n", fp_strrchr(s, 'o'));
    printf("%s\n", fp_strstr(s, "wor"));

    return 0;
}
