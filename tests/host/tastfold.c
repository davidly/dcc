#include <stdio.h>

static int failures;
static volatile int runtime_signed_int = -32768;
static volatile int runtime_minus_one = -1;
static volatile unsigned int runtime_unsigned_int = 0x8000U;
static volatile long runtime_signed_long = (-2147483647L - 1L);
static volatile unsigned long runtime_unsigned_long = 0x80000000UL;

static void check(const char *name, long folded, long runtime)
{
    if (folded != runtime) {
        printf("FAIL %s: %ld != %ld\n", name, folded, runtime);
        ++failures;
    }
}

int main(void)
{
    check("signed int right shift",
          (int)0x8000U >> 15, runtime_signed_int >> 15);
    check("unsigned int right shift",
          0x8000U >> 15, runtime_unsigned_int >> 15);
    check("signed long right shift",
          (-2147483647L - 1L) >> 31, runtime_signed_long >> 31);
    check("unsigned long right shift",
          0x80000000UL >> 31, runtime_unsigned_long >> 31);
    check("unsigned int wrap", 65535U + 1U,
          runtime_unsigned_int + runtime_unsigned_int);
    check("mixed equality", -1 == 65535U,
          runtime_minus_one == 65535U);

    if (failures != 0)
        return 1;
    puts("tastfold passed");
    return 0;
}
