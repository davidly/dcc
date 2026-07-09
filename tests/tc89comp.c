/* tc89comp.c - compound assignment conversion/pointer regression */

#include <stdio.h>
#include <stdint.h>

static int fails;

static void cki(const char *name, int got, int expect)
{
    if (got != expect) {
        printf("FAIL %s got %d expected %d\n", name, got, expect);
        fails++;
    }
}

static void cku(const char *name, unsigned int got, unsigned int expect)
{
    if (got != expect) {
        printf("FAIL %s got %u expected %u\n", name, got, expect);
        fails++;
    }
}

static void ckl(const char *name, long got, long expect)
{
    if (got != expect) {
        printf("FAIL %s got %ld expected %ld\n", name, got, expect);
        fails++;
    }
}

static void cai1(void)
{
    int i;
    long l;

    i = 10;
    l = 30000L;
    i += l;
    cki("i_plus_l", i, 30010);

    i = 7;
    l = 3000L;
    i *= l;
    cki("i_mul_l", i, 21000);

    i = 10;
    l = 3L;
    i /= l;
    cki("i_div_l", i, 3);

    i = -10;
    l = 3L;
    i %= l;
    cki("i_mod_l", i, -1);

    i = 7000;
    l = 70000L;
    i /= l;
    cki("i_div_big_l", i, 0);

    i = 7000;
    l = 70000L;
    i %= l;
    cki("i_mod_big_l", i, 7000);
}

static void cau2(void)
{
    unsigned int u;
    int s;

    u = 10;
    s = -3;
    u += s;       /* unsigned 16-bit operation */
    cku("u_plus_s", u, 7U);

    u = 10;
    s = -3;
    u /= s;       /* 10 / 65533 == 0 */
    cku("u_div_s", u, 0U);

    u = 10;
    s = -3;
    u %= s;       /* 10 % 65533 == 10 */
    cku("u_mod_s", u, 10U);
}

static void cal3(void)
{
    long l;
    int i;
    unsigned int u;

    l = 100000L;
    i = -5;
    l += i;
    ckl("l_plus_i", l, 99995L);

    l = 100000L;
    u = 65535U;
    l += u;       /* long can represent all unsigned int values */
    ckl("l_plus_u", l, 165535L);

    l = -100000L;
    i = 3;
    l /= i;
    ckl("l_div_i", l, -33333L);
}

static void cap4(void)
{
    int a[8];
    int *p;
    long la[8];
    long *lp;

    a[0] = 11; a[1] = 22; a[2] = 33; a[3] = 44; a[4] = 55;
    p = a;
    p += 3;
    cki("ptr_plus_eq", *p, 44);
    p -= 2;
    cki("ptr_minus_eq", *p, 22);

    la[0] = 1000L; la[1] = 2000L; la[2] = 3000L; la[3] = 4000L;
    lp = la;
    lp += 2;
    ckl("lptr_plus_eq", *lp, 3000L);
    lp -= 1;
    ckl("lptr_minus_eq", *lp, 2000L);
}

struct Slice {
    const int *data;
    int count;
};

static struct Slice drop_first(struct Slice s, int n)
{
    if (n > s.count)
        n = s.count;
    s.data += n;
    s.count -= n;
    return s;
}

static int slice_sum(struct Slice s)
{
    int i;
    int total;

    total = 0;
    for (i = 0; i < s.count; ++i)
        total += s.data[i];
    return total;
}

static int slice_max(struct Slice s)
{
    int i;
    int best;

    best = s.data[0];
    for (i = 1; i < s.count; ++i) {
        if (s.data[i] > best)
            best = s.data[i];
    }
    return best;
}

static void cas5(void)
{
    static const int values[] = { 4, 8, 15, 16, 23, 42 };
    struct Slice s;
    struct Slice tail;

    s.data = values;
    s.count = 6;
    tail = drop_first(s, 2);
    cki("slice_ptr_member_plus_eq_first", tail.data[0], 15);
    cki("slice_ptr_member_plus_eq_sum", slice_sum(tail), 96);
    cki("slice_ptr_member_plus_eq_max", slice_max(tail), 42);
    cki("slice_ptr_member_plus_eq_count", tail.count, 4);
}

int main(void)
{
    printf("tc89comp start\n");
    fails = 0;
    cai1();
    cau2();
    cal3();
    cap4();
    cas5();
    if (fails) {
        printf("tc89comp failed: %d\n", fails);
        return 1;
    }
    printf("tc89comp completed with great success\n");
    return 0;
}
