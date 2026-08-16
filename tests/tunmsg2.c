/*
 * tunmsg2.c - overlapping union-member initializer regression.
 * Generated archive case: batch5/c1113.
 */
#include <stdio.h>

struct Message {
    int type;
    union {
        char text[8];
        struct {
            int code;
            int detail;
        };
    };
};

int main(void)
{
    struct Message message = {
        .type = 2, .code = 404, .detail = 7
    };
    int ok = message.code == 404 && message.detail == 7;

    printf("tunmsg2 message=%d,%d\n",
           message.code, message.detail);
    if (ok)
        printf("tunmsg2 passed with great success\n");
    return !ok;
}
