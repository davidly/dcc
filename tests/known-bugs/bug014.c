/* BUG-014: a signed 32-bit local that has two reaching definitions, is then
 * incremented by one and stored to a global, stops the compile with
 * "missing direct MIR named-home offset". Every spelling of the increment
 * fails. Decrementing, adding 2, an unsigned local, or storing to an array
 * element all compile. */
#include <stdint.h>
#include <stdio.h>

static int32_t g = 65536;
static int16_t sel = 1;

static int32_t post_increment(void)
{
    int32_t l = 1;
    if (sel)
        l = 70000;
    l++;
    g = l;
    return l;
}

static int32_t add_one(void)
{
    int32_t l = 1;
    if (sel)
        l = 5;
    l += 1;
    g = l;
    return l;
}

static int32_t assigned_pre_increment(void)
{
    int32_t l = 1;
    switch (sel) {
    case 0:
        break;
    case 1:
        l = 7;
        break;
    }
    g = ++l;
    return l;
}

static int32_t after_goto(void)
{
    int32_t l = 1;
    if (sel == 0)
        goto skip;
    l = 9;
skip:
    g = (++l);
    return l;
}

int main(void)
{
    printf("post-increment %ld %ld\n", (long)post_increment(), (long)g);
    printf("add one %ld %ld\n", (long)add_one(), (long)g);
    printf("assigned pre-increment %ld %ld\n", (long)assigned_pre_increment(), (long)g);
    printf("after goto %ld %ld\n", (long)after_goto(), (long)g);
    return 0;
}
