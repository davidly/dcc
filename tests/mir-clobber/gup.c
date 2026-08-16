#include <stdio.h>
#include <string.h>
#define MAXNAME 32

long toupper(long c);

static void upcase(char *d, const char *s)
{
    int i;
    for (i = 0; s[i] && i < MAXNAME - 1; i++)
        d[i] = (char)toupper((unsigned char)s[i]);
    d[i] = 0;
}

int main(void)
{
    char out[MAXNAME];
    upcase(out, "abcd");
    printf("GUP=%s\n", out);
    return strcmp(out, "!!!!") != 0;
}
