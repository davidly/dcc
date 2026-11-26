/* Preserve float division when both operands are prefix updates of fields. */
#include <stdint.h>
#include <stdio.h>

struct Values {
    float v[2];
};

static struct Values left = { { 1.5f, 0.0f } };
static struct Values right[2] = { { { 0.0f, 0.0f } }, { { 0.0f, 3.0f } } };

int main(void)
{
    float ratio;

    {
        int16_t index = 1;
        ratio = ++left.v[0] / --right[index].v[1];
    }

    printf("%d %d %d\n", (int)(ratio * 4.0f),
           (int)(left.v[0] * 4.0f), (int)(right[1].v[1] * 4.0f));
    return 0;
}
