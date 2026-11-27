/* Float oracle seed 395657 put nested unary operators over a division that
 * cannot be folded in an untaken branch. */
#include <stdint.h>
#include <stdio.h>

static float result = 4.0f;

static void guarded(float value)
{
    if ((int32_t)value == 9) {
        result = -(-(-32768.0f / 0.0f));
        (void)-(-(-1.0f / 0.0f));
    } else {
        result = -(+(value / 2.0f));
    }
}

int main(void)
{
    guarded(7.0f);
    printf("%d\n", (int)(result * 2.0f));
    return 0;
}
