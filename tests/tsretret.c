#include <stdio.h>
#include <stdint.h>

struct Pair { int32_t first; int32_t second; };

static struct Pair make_pair(int32_t first, int32_t second)
{
    struct Pair result;
    result.first = first;
    result.second = second;
    return result;
}

static struct Pair normalize(struct Pair value)
{
    if (value.second < 0) {
        value.first = -value.first;
        value.second = -value.second;
    }
    return value;
}

static struct Pair make_normal(int32_t first, int32_t second)
{
    struct Pair result = make_pair(first, second);
    return normalize(result);
}

static struct Pair chain(int depth, struct Pair value)
{
    if (depth == 0)
        return normalize(value);
    value.first += 1;
    return chain(depth - 1, value);
}

static struct Pair nested(void)
{
    return normalize(make_pair(-9, -4));
}

int main(void)
{
    struct Pair a = make_normal(6, -8);
    struct Pair b = chain(3, make_pair(10, 2));
    struct Pair c = nested();

    printf("tsretret a=%ld,%ld b=%ld,%ld c=%ld,%ld\n",
           (long)a.first, (long)a.second, (long)b.first,
           (long)b.second, (long)c.first, (long)c.second);
    return 0;
}
