#ifdef RAWCONV_RENAMED
#define RAWCONV_FUNCTION renamed_raw_conversion_check_fixture
#else
#define RAWCONV_FUNCTION raw_conversion_check_fixture
#endif
#define main RAWCONV_FUNCTION

#include "../tfpraw.c"

#undef main

static void oracle(int condition, int *failures)
{
    if (!condition)
        ++*failures;
}

int main(void)
{
    int failures = 0;

    oracle(RAWCONV_FUNCTION() == 0, &failures);
    oracle(fpzero(0x00000000L) != 0, &failures);
    oracle(fpzero(0x80000000L) != 0, &failures);
    oracle(fpnan(0x7fc00000L) != 0, &failures);
    oracle(fpnan(0x7f800000L) == 0, &failures);
    oracle(fpinf(0x7f800000L) != 0, &failures);
    oracle(fpinf(0x7fc00000L) == 0, &failures);
    oracle(fpsgn(0x80000000L) != 0, &failures);
    oracle((unsigned long)fpitof(1) == 0x3f800000UL, &failures);
    oracle((unsigned long)fpitof(-1) == 0xbf800000UL, &failures);
    oracle((unsigned long)fpltof(1000000L) == 0x49742400UL,
           &failures);
    oracle((unsigned long)fpltof(-1000000L) == 0xc9742400UL,
           &failures);
    printf("raw conversion oracle failures=%d\n", failures);
    return failures;
}
