#include <stdio.h>

int main(int argc, char *argv[])
{
#ifdef MIR_CLOBBER_ARGV_EXTRA
    printf("argv extra\n");
#endif
    printf("argc: %d\n", argc);
    for (int i = 0; i < argc; i++)
        printf("argv[ %d ]: '%s'\n", i, argv[i]);
    printf("targs completed with great success\n");
    return 0;
}
