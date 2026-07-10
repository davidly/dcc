#include <stdio.h>
#include <stdbool.h>

static bool palindrome(const char *text)
{
    int length = 0;
    while (text[length])
        ++length;
    for (int i = 0; i < length / 2; ++i)
        if (text[i] != text[length - 1 - i])
            return false;
    return true;
}

int main(void)
{
    bool a = palindrome("level");
    bool b = palindrome("lever");
    printf("tforfrm palindrome=%d,%d\n", (int)a, (int)b);
    return !(a && !b);
}