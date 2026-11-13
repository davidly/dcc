/* BUG-033: a constant divided by a constant zero is rejected with DCC-E1002
 * ("unsupported if condition or branch"), even in code that never runs. The
 * division is undefined only if it is evaluated, so the program is valid,
 * and the message does not mention division. `n / 0` with a variable `n` is
 * accepted. Low priority. */
#include <stdint.h>
#include <stdio.h>

static int16_t never = 0;
static int16_t out;

static int16_t in_return(void)
{
    if (never)
        return 1000 / 0;
    return 1;
}

static void in_assignment(void)
{
    if (never)
        out = 7 % 0;
    else
        out = 2;
}

int main(void)
{
    in_assignment();
    printf("%d %d\n", in_return(), out);
    return 0;
}
