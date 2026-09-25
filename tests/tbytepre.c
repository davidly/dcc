/* Issue #197: prefix byte updates must return the narrowed stored value. */
#include <stdio.h>

static unsigned char global_byte;

static int truth_return(unsigned char i)
{
    return ++i ? 1 : 0;
}

static int truth_pair(unsigned char i, unsigned char j)
{
    return ++i && ++j;
}

static int increment_loop(void)
{
    unsigned char i = 0;
    unsigned int count = 0;
    do {
        if (++count > 256)
            return 0;
    } while (++i);
    return count == 256 && i == 0;
}

static int decrement_loop(void)
{
    unsigned char i = 0;
    unsigned int count = 0;
    do {
        if (++count > 256)
            return 0;
    } while (--i);
    return count == 256 && i == 0;
}

int main(void)
{
    unsigned char local = 255;
    unsigned char bytes[2];
    unsigned char *p = bytes;
    struct { unsigned char value; } s;
    signed char signed_byte = -1;
    int failures = 0;

    if (!increment_loop() || !decrement_loop())
        failures |= 1;
    if (++local != 0 || local != 0)
        failures |= 2;
    if (--local != 255 || local != 255)
        failures |= 4;
    global_byte = 255;
    if (++global_byte != 0 || --global_byte != 255)
        failures |= 8;
    bytes[0] = 255;
    bytes[1] = 0;
    if (++*p++ != 0 || p != bytes + 1 || bytes[0] != 0)
        failures |= 16;
    if (--bytes[1] != 255 || bytes[1] != 255)
        failures |= 32;
    s.value = 255;
    if (++s.value != 0 || --s.value != 255)
        failures |= 64;
    if (++signed_byte != 0 || --signed_byte != -1)
        failures |= 128;
    local = 255;
    if (local++ != 255 || local != 0 || local-- != 0 || local != 255)
        failures |= 256;
    if (truth_return(255) != 0 || truth_return(254) != 1 ||
        truth_pair(255, 0) != 0 || truth_pair(0, 255) != 0 ||
        truth_pair(0, 0) != 1)
        failures |= 512;
    local = 255;
    if (256 + ++local != 256)
        failures |= 1024;
    local = 255;
    if (!++local != 1)
        failures |= 2048;
    printf("tbytepre failures: %d\n", failures);
    return failures != 0;
}
