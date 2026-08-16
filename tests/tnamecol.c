/*
 * tnamecol.c - local/global symbol name collision regression.
 * Generated archive case: batch4/c1104.
 */
#include <stdio.h>

enum JobKind {
    JOB_COPY,
    JOB_DELETE
};

struct Job {
    enum JobKind kind;
    union {
        struct {
            int source;
            int target;
        };
        int file;
    };
};

int main(void)
{
    struct Job copy = { JOB_COPY, { .source = 2, .target = 9 } };
    struct Job remove = { JOB_DELETE, { .file = 7 } };
    int ok;

    printf("tnamecol jobs=%d,%d/%d,%d\n",
           copy.source, copy.target, remove.kind, remove.file);
    ok = copy.source == 2 && copy.target == 9 &&
         remove.kind == JOB_DELETE && remove.file == 7;
    if (ok)
        printf("tnamecol passed with great success\n");
    return !ok;
}
