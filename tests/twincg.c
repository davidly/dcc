/* Wide local increments retain their named home through branch joins,
 * switches, gotos and loops, including carry and unsigned wraparound.
 * Cover increment spellings and neighbouring decrement/add-two controls. */
#include <stdint.h>
#include <stdio.h>

static int32_t g = 65536;
static int16_t sel = 1;
static uint32_t ug;

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

static int32_t assigned_add(void)
{
    int32_t l = 1;
    if (sel) l = 65535;
    l = l + 1;
    g = l;
    return l;
}
static uint32_t unsigned_wrap(void)
{
    uint32_t l = 1;
    if (sel) l = UINT32_MAX;
    ug = ++l;
    return l;
}
static int32_t in_loop(void)
{
    int32_t l = 65534;
    int16_t n = 0;
    do { g = ++l; } while (++n < 3);
    return l;
}
static int32_t controls(void)
{
    int32_t l = 1;
    if (sel) l = 70000;
    --l;
    l += 2;
    g = l;
    return l;
}
int main(void)
{
    int32_t r;
    uint32_t u;
    printf("post-increment %ld %ld\n", (long)post_increment(), (long)g);
    printf("add one %ld %ld\n", (long)add_one(), (long)g);
    printf("assigned pre-increment %ld %ld\n", (long)assigned_pre_increment(), (long)g);
    printf("after goto %ld %ld\n", (long)after_goto(), (long)g);
    r = assigned_add();
    printf("assigned add %ld %ld\n", (long)r, (long)g);
    u = unsigned_wrap();
    printf("unsigned wrap %lu %lu\n", (unsigned long)u, (unsigned long)ug);
    r = in_loop();
    printf("loop %ld %ld\n", (long)r, (long)g);
    r = controls();
    printf("controls %ld %ld\n", (long)r, (long)g);
    sel = 0;
    r = post_increment();
    printf("untaken %ld %ld\n", (long)r, (long)g);
    return 0;
}
