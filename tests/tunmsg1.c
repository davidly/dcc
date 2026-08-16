/*
 * tunmsg1.c - designated anonymous-union initializer regression.
 * Generated archive case: batch4/c1113.
 */
#include <stdio.h>

struct Message {
    int kind;
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
    struct Message text = {
        .kind = 1, .text = { 'O', 'K', '\0' }
    };
    struct Message error = {
        .kind = 2, .code = 404, .detail = 7
    };
    int ok;

    printf("tunmsg1 message=%s/%d,%d\n",
           text.text, error.code, error.detail);
    ok = text.text[0] == 'O' && text.text[1] == 'K' &&
         error.code == 404 && error.detail == 7;
    if (ok)
        printf("tunmsg1 passed with great success\n");
    return !ok;
}
