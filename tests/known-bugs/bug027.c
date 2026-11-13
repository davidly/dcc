/* BUG-027: a 16-bit local that is assigned in an if-else nested inside
 * another `if`, and then updated, loses the assignment when a loop (or a
 * conditional expression) follows: the value merged from the two arms is
 * never stored, and the update is applied to the old value. It is correct
 * without the outer `if`, when the inner `if` has no `else`, and when no
 * loop follows. */
#include <stdint.h>
#include <stdio.h>

static int16_t one = 1;
static int16_t zero = 0;
static int16_t seen;
static int32_t wide;

static int16_t then_arm(int16_t n)
{
    int16_t l = 0;
    int16_t i;
    if (one) {
        if (one) {
            l = n;
        } else {
        }
        l++;
    }
    for (i = 0; i < 2; i++)
        seen = l;
    return l;
}

static int16_t both_arms(int16_t n)
{
    int16_t l = 0;
    int16_t i;
    if (one) {
        if (one)
            l = n;
        else
            l = 2;
        l++;
    }
    for (i = 0; i < 2; i++)
        seen = l;
    return l;
}

static int16_t else_arm(int16_t n)
{
    int16_t l = 0;
    int16_t i;
    if (one) {
        if (zero) {
        } else {
            l = n;
        }
        l += 2;
    }
    for (i = 0; i < 2; i++)
        seen = l;
    return l;
}

static int16_t assigned_sum(int16_t n)
{
    int16_t l = 0;
    int16_t i;
    if (one) {
        if (one) {
            l = n;
        } else {
        }
        l = l + 1;
    }
    for (i = 0; i < 2; i++)
        seen = l;
    return l;
}

static int16_t parameter(uint16_t x, int16_t n)
{
    if (one) {
        if (one) {
            x = 230;
        } else {
        }
        ++x;
    }
    wide = (n ? x : 1);
    return 1;
}

/* correct: no outer `if` */
static int16_t no_outer_if(int16_t n)
{
    int16_t l = 0;
    int16_t i;
    if (one) {
        l = n;
    } else {
    }
    l++;
    for (i = 0; i < 2; i++)
        seen = l;
    return l;
}

/* correct: the inner `if` has no `else` */
static int16_t no_else(int16_t n)
{
    int16_t l = 0;
    int16_t i;
    if (one) {
        if (one) {
            l = n;
        }
        l++;
    }
    for (i = 0; i < 2; i++)
        seen = l;
    return l;
}

int main(void)
{
    int16_t r;
    r = then_arm(9);
    printf("then arm %d %d\n", r, seen);
    r = both_arms(9);
    printf("both arms %d %d\n", r, seen);
    r = else_arm(9);
    printf("else arm %d %d\n", r, seen);
    r = assigned_sum(9);
    printf("l = l + 1 %d %d\n", r, seen);
    r = parameter(0xdf7e, 9);
    printf("parameter %ld\n", (long)wide);
    r = no_outer_if(9);
    printf("no outer if %d %d\n", r, seen);
    r = no_else(9);
    printf("no else %d %d\n", r, seen);
    return 0;
}
