/*
 * tvlapal.c - bool VLA dynamic-programming regression.
 * Generated archive case: batch9/c9930.
 */
#include <stdbool.h>
#include <stdio.h>

static int palindrome_length(int count, const char text[count])
{
    bool table[count][16];
    int best = 1;

    for (int index = 0; index < count; ++index)
        table[index][index] = true;
    for (int length = 2; length <= count; ++length)
        for (int left = 0; left + length <= count; ++left) {
            int right = left + length - 1;
            table[left][right] =
                text[left] == text[right] &&
                (length == 2 || table[left + 1][right - 1]);
            if (table[left][right])
                best = length;
        }
    return best;
}

int main(void)
{
    char text[] = "BANANAS";
    int length = palindrome_length(7, text);

    printf("tvlapal palindrome=%d\n", length);
    if (length == 5)
        printf("tvlapal passed with great success\n");
    return length != 5;
}
