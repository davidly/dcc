/* Float oracle seed 395657 put nested unary operators over a division that
 * cannot be folded in an untaken branch. */
#include <stdint.h>
#include <stdio.h>

static float result = 4.0f;

#ifdef _MSC_VER
/* MSVC rejects literal division by zero even in this untaken branch.
 * Keep the literal expression for DCC's constant-folding regression. */
static volatile float guarded_zero = 0.0f;
#else
#define guarded_zero 0.0f
#endif

static void guarded(float value)
{
    if ((int32_t)value == 9) {
        result = -(-(-32768.0f / guarded_zero));
        (void)-(-(-1.0f / guarded_zero));
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
