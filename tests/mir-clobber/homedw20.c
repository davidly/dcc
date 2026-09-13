#include <stdio.h>

#ifdef H20_SIGNED_BYTE
typedef signed char H20Byte;
#else
typedef unsigned char H20Byte;
#endif

static volatile H20Byte byte_value = 5;
static int word_value = 7;

#ifdef H20_INDIRECT_BYTE
int homed_wave20(H20Byte *location, H20Byte expected)
{
    int prior = (int)*location;

    if (prior == (int)expected) {
        word_value = prior + word_value;
        *location = (H20Byte)word_value;
        return (int)*location;
    }
    return prior;
}
#else
int homed_wave20(H20Byte expected)
{
    int prior = (int)byte_value;

    if (prior == (int)expected) {
        word_value = prior + word_value;
        return word_value;
    }
    return prior;
}
#endif

int main(void)
{
    int first;
    int second;

#ifdef H20_INDIRECT_BYTE
    H20Byte local = 5;

    first = homed_wave20(&local, 5);
    second = homed_wave20(&local, 4);
    if (first != 12 || second != 12 ||
        (int)local != 12 || word_value != 12) {
        printf("FAIL homed_wave20 %d %d %d %d\n",
               first, second, (int)local, word_value);
        return 1;
    }
#else
    first = homed_wave20(5);
    second = homed_wave20(4);
    if (first != 12 || second != 5 || word_value != 12) {
        printf("FAIL homed_wave20 %d %d %d\n",
               first, second, word_value);
        return 1;
    }
#endif
    printf("PASS homed_wave20\n");
    return 0;
}
