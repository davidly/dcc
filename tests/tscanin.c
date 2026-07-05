#include <stdio.h>

/* Console-stdin regression for the runtime _read -> __gchr path used by the
 * scanf family and getchar.  That path once used a nonblocking BDOS call that
 * reported phantom bytes instead of waiting for input; drive it here with
 * piped stdin so the blocking behavior stays covered.
 *
 * The blocking console read (BDOS function 1) echoes each consumed character,
 * so the expected output interleaves the echoed input with the program's own
 * lines.  The input is newline-terminated and fully consumed so the test never
 * reads past end-of-input. */
int main(void)
{
    int a = 0, b = 0, n;
    char word[16];

    n = scanf("%d %d", &a, &b);
    printf("scanf n=%d sum=%d\n", n, a + b);

    n = scanf("%15s", word);
    printf("scanf n=%d word=%s\n", n, word);

    return 0;
}
