/* Isolated runtime and structural controls for the direct byte-sum schedule. */

#include <stdio.h>

#ifdef DBYTESUM_UNSIGNED
typedef unsigned char dbytesum_char;
#else
typedef char dbytesum_char;
#endif

#ifdef DBYTESUM_VOLATILE
#define DBYTESUM_QUALIFIER volatile
#else
#define DBYTESUM_QUALIFIER
#endif

#ifdef DBYTESUM_RENAMED
#define DBYTESUM_FUNCTION fixture_direct_byte_sum_renamed
#else
#define DBYTESUM_FUNCTION fixture_direct_byte_sum
#endif

static int DBYTESUM_FUNCTION(DBYTESUM_QUALIFIER dbytesum_char *p, int n)
{
    int total;
    int i;

    total = 0;
    for (i = 0; i < n; i++)
#ifdef DBYTESUM_EXTRA_CFG
    {
        if (n == 12345)
            return -99;
#endif
#ifdef DBYTESUM_ADD_ALL
        total = total + p[i];
#elif defined(DBYTESUM_ZERO_ONLY)
        if (p[i] == 0)
            total = total + p[i];
#else
        if (p[i] != 0)
            total = total + p[i];
#endif
#ifdef DBYTESUM_EXTRA_CFG
    }
#endif
    return total;
}

static int reference_sum(DBYTESUM_QUALIFIER dbytesum_char *p, int n)
{
    int total;
    int i;

    total = 0;
    for (i = 0; i < n; i++) {
#ifdef DBYTESUM_EXTRA_CFG
        if (n == 12345)
            return -99;
#endif
#ifdef DBYTESUM_ADD_ALL
        total = total + p[i];
#elif defined(DBYTESUM_ZERO_ONLY)
        if (p[i] == 0)
            total = total + p[i];
#else
        if (p[i] != 0)
            total = total + p[i];
#endif
    }
    return total;
}

static unsigned int checksum = 811U;
static int failures;

static void check_case(
    DBYTESUM_QUALIFIER dbytesum_char *values, int count)
{
    int got;
    int reference;

    got = DBYTESUM_FUNCTION(values, count);
    reference = reference_sum(values, count);
    checksum = (unsigned int)(
        checksum * 113U + (unsigned int)got +
        (unsigned int)reference + (unsigned int)count + 41U);
    if (got != reference)
        ++failures;
}

int main(void)
{
    dbytesum_char signed_values[12] = {
        91, 128, 0, 249, 1, 0, 126, 63, 224, 5, 245, 169
    };
    dbytesum_char positive_values[10] = {
        73, 1, 0, 2, 0, 3, 4, 0, 5, 182
    };

    check_case(signed_values + 1, 10);
    check_case(signed_values + 2, 5);
    check_case(signed_values + 4, 0);
    check_case(signed_values + 6, -3);
    check_case(signed_values + 1, 1);
    check_case(positive_values + 1, 8);
    printf("direct byte sum failures=%d checksum=%u guards=%d,%d\n",
           failures, checksum,
           (int)signed_values[0], (int)positive_values[9]);
    return failures != 0;
}
