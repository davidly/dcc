#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Direct strcmp calls use the __smf DE/HL fastcall. Taking strcmp's address
 * must retain the public __scmp caller-cleanup ABI for indirect calls. */
int (*fp_strcmp)(const char *, const char *) = strcmp;
int (*fp_strncmp)(const char *, const char *, size_t) = strncmp;
void *(*fp_malloc)(size_t) = malloc;
void (*fp_free)(void *) = free;

int main(void)
{
    char *p;

    printf("direct %d %d %d %d %d\n",
           strcmp("abc", "abc"),
           strcmp("abc", "abd"),
           strcmp("abd", "abc"),
           strcmp("", "a"),
           strcmp("a", ""));
    printf("indirect %d %d %d %d %d\n",
           fp_strcmp("abc", "abc"),
           fp_strcmp("abc", "abd"),
           fp_strcmp("abd", "abc"),
           fp_strcmp("", "a"),
           fp_strcmp("a", ""));
    printf("strncmp %d %d %d %d\n",
           strncmp("abc", "abd", 2) == 0,
           strncmp("abc", "abd", 3) < 0,
           fp_strncmp("abd", "abc", 3) > 0,
           fp_strncmp("abc", "abd", 0) == 0);
    p = malloc(4);
    if (p) {
        strcpy(p, "ok");
        printf("malloc %s\n", p);
    }
    free(p);
    p = fp_malloc(4);
    if (p) {
        strcpy(p, "fp");
        printf("indirect malloc %s\n", p);
    }
    fp_free(p);
    return 0;
}
