#include "tfileapp.h"

void tf_fill_pattern(unsigned char *buffer, int length)
{
    int index;

    for (index = 0; index < length; ++index)
        buffer[index] = (unsigned char)((index * 37 + 11) & 255);
}

unsigned long tfsum_checksum(const unsigned char *buffer, int length)
{
    unsigned long sum = 0;
    int index;

    for (index = 0; index < length; ++index)
        sum = (sum * 33UL) ^ buffer[index];
    return sum;
}
