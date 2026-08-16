#include <stdio.h>
#include <string.h>

static int custom_bdos(int fn, long dearg)
{
    printf("BDOS:%d:%ld\n", fn, dearg);
    return dearg == 81L ? 81 : 0;
}

size_t (*fp_strlen)(const char *) = strlen;
char *(*fp_strchr)(const char *, int) = strchr;
int (*fp_memcmp)(const void *, const void *, size_t) = memcmp;
void *(*fp_memset)(void *, int, size_t) = memset;
int (*fp_bdos)(int, long) = custom_bdos;
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
    printf("%c%c%c%c%c%c%c%c\n", msetbuf[0], msetbuf[1], msetbuf[2], msetbuf[3], msetbuf[4], msetbuf[5], msetbuf[6], msetbuf[7]);
    fp_bdos(2, 81L);
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
