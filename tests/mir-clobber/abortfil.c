#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define isgraph(c) (isprint(c) && !isspace(c))

static int failures;

static void check(const char *name, int got, int expected)
{
    if (!!got != !!expected) {
        printf("FAIL %s: got %d expected %d\n", name, got, expected);
        failures++;
    }
}

int main(void)
{
    FILE *file;
    char buffer[8];

    file = fopen("RNOLD.TMP", "w");
    fputs("hello", file);
    fclose(file);
    check("rename_ret", rename("RNOLD.TMP", "RNNEW.TMP"), 0);
    file = fopen("RNNEW.TMP", "r");
    if (!file) {
        printf("FAIL rename: new file not found\n");
        failures++;
    } else {
        fgets(buffer, sizeof(buffer), file);
        fclose(file);
        check("rename_content", strcmp(buffer, "hello"), 0);
    }
    file = fopen("RNOLD.TMP", "r");
    if (file) {
        printf("FAIL rename: old file still exists\n");
        fclose(file);
        failures++;
    }
    remove("RNNEW.TMP");
    check("isgraph_A", isgraph('A'), 1);
    check("isgraph_z", isgraph('z'), 1);
    check("isgraph_0", isgraph('0'), 1);
    check("isgraph_bang", isgraph('!'), 1);
    check("isgraph_sp", isgraph(' '), 0);
    check("isgraph_tab", isgraph('\t'), 0);
    check("isgraph_nul", isgraph('\0'), 0);
    if (failures) {
        printf("abort file FAILED %d\n", failures);
        return 1;
    }
#ifdef MIR_CLOBBER_ABORT_EXTRA
    printf("abort extra control\n");
#endif
    printf("abort file ok\n");
    abort();
    printf("FAIL abort: returned\n");
    return 1;
}
