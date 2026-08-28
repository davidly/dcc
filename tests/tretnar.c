/* Integer narrowing at a byte-sized function return.
 * Adapted from SDCC's GCC torture execute test pr39240. */
#include <stdio.h>

static signed char signed_from_int(int value)
{
    return value;
}

static unsigned char unsigned_from_int(int value)
{
    return value;
}

static signed char signed_from_long(long value)
{
    return value;
}

static unsigned char unsigned_from_long(long value)
{
    return value;
}

static signed char signed_literal(void)
{
    return -1;
}

int main(void)
{
    int failures = 0;

    if (signed_from_int(-4) != -4)
        ++failures;
    if (unsigned_from_int(-4) != 252)
        ++failures;
    if (signed_from_long(100L) != 100)
        ++failures;
    if (unsigned_from_long(257L) != 1)
        ++failures;
    if (signed_literal() != -1)
        ++failures;

    if (failures) {
        printf("tretnar failed: %d\n", failures);
        return 1;
    }
    printf("tretnar completed with great success\n");
    return 0;
}
