#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHILD_ARG "XCHILD"

static int run_as_child(int argc, char **argv)
{
    int i;
    int len;
    char *tail;
    char buf[128];

    len = *(unsigned char *)0x80;
    tail = (char *)0x81;
    if (len > 126)
        len = 126;
    for (i = 0; i < len; ++i)
        buf[i] = tail[i];
    buf[i] = 0;

    printf("child: tail='%s'\n", buf);
    printf("child: argc=%d\n", argc);
    for (i = 0; i < argc; ++i)
        printf("child: argv[%d]='%s'\n", i, argv[i]);
    if (argc < 2) {
        printf("FAIL: child argc %d < 2\n", argc);
        return 1;
    }
    if (strcmp(argv[1], CHILD_ARG) != 0) {
        printf("FAIL: argv[1]='%s' expected '%s'\n",
               argv[1], CHILD_ARG);
        return 1;
    }
    printf("child: pass\n");
    return 0;
}

int main(int argc, char **argv)
{
    int result;
    char *arguments[3];

    if (argc >= 2 && strcmp(argv[1], CHILD_ARG) == 0)
        return run_as_child(argc, argv);
#ifdef MIR_CLOBBER_EXEC_EXTRA
    printf("parent: extra control\n");
#endif
    printf("parent: exec missing file\n");
    result = exec("nosuchfi", "");
    if (result != -1) {
        printf("FAIL: exec missing returned %d\n", result);
        return 1;
    }
    printf("parent: execv missing file\n");
    arguments[0] = "nosuchfi";
    arguments[1] = (char *)0;
    result = execv("nosuchfi", arguments);
    if (result != -1) {
        printf("FAIL: execv missing returned %d\n", result);
        return 1;
    }
    printf("parent: exec self as child\n");
#ifdef MIR_CLOBBER_EXEC_EXTRA
    exec("execargv", " " CHILD_ARG);
#else
    exec("execarg", " " CHILD_ARG);
#endif
    printf("FAIL: exec self returned\n");
    return 1;
}
